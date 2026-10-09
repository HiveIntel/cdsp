#include "serd_rdf_reader.h"

#include <serd/serd.h>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace rdf_mapper {
namespace {

struct ParseContext {
    SerdEnv* environment = nullptr;
    std::vector<RDFTriple> triples;
    std::string callback_error;
    std::string syntax_error;
};

std::string nodeText(const SerdNode& node) {
    if (node.buf == nullptr) {
        return {};
    }

    return {reinterpret_cast<const char*>(node.buf), node.n_bytes};
}

void rememberCallbackError(ParseContext& context, const std::string& message) noexcept {
    if (!context.callback_error.empty()) {
        return;
    }

    try {
        context.callback_error = message;
    } catch (...) {
        // The callback must never allow a C++ exception to cross the C API.
    }
}

std::optional<std::string> expandedIri(ParseContext& context, const SerdNode& node,
                                       const char* role) {
    if (node.type != SERD_URI && node.type != SERD_CURIE) {
        rememberCallbackError(context, std::string("Invalid RDF ") + role + " node type");
        return std::nullopt;
    }

    SerdNode expanded = serd_env_expand_node(context.environment, &node);
    if (expanded.buf == nullptr) {
        rememberCallbackError(
            context, std::string("Unable to expand RDF ") + role + " '" + nodeText(node) + "'");
        return std::nullopt;
    }

    std::string value;
    try {
        value = nodeText(expanded);
    } catch (...) {
        serd_node_free(&expanded);
        throw;
    }
    serd_node_free(&expanded);
    return value;
}

std::optional<RDFNode> resourceNode(ParseContext& context, const SerdNode& node, const char* role,
                                    bool allow_blank_node) {
    if (node.type == SERD_BLANK && allow_blank_node) {
        return RDFNode::blankNode(nodeText(node));
    }

    const auto iri = expandedIri(context, node, role);
    if (!iri.has_value()) {
        return std::nullopt;
    }

    return RDFNode::iri(*iri);
}

SerdStatus onBase(void* handle, const SerdNode* uri) noexcept {
    auto& context = *static_cast<ParseContext*>(handle);

    try {
        const SerdStatus status = serd_env_set_base_uri(context.environment, uri);
        if (status != SERD_SUCCESS) {
            rememberCallbackError(context, "Unable to set Turtle base IRI");
        }
        return status;
    } catch (const std::exception& error) {
        rememberCallbackError(context, error.what());
    } catch (...) {
        rememberCallbackError(context, "Unknown error in Turtle base callback");
    }

    return SERD_ERR_UNKNOWN;
}

SerdStatus onPrefix(void* handle, const SerdNode* name, const SerdNode* uri) noexcept {
    auto& context = *static_cast<ParseContext*>(handle);

    try {
        const SerdStatus status = serd_env_set_prefix(context.environment, name, uri);
        if (status != SERD_SUCCESS) {
            rememberCallbackError(context, "Unable to set Turtle prefix '" + nodeText(*name) + "'");
        }
        return status;
    } catch (const std::exception& error) {
        rememberCallbackError(context, error.what());
    } catch (...) {
        rememberCallbackError(context, "Unknown error in Turtle prefix callback");
    }

    return SERD_ERR_UNKNOWN;
}

SerdStatus onStatement(void* handle, SerdStatementFlags flags, const SerdNode* graph,
                       const SerdNode* subject, const SerdNode* predicate, const SerdNode* object,
                       const SerdNode* object_datatype, const SerdNode* object_language) noexcept {
    auto& context = *static_cast<ParseContext*>(handle);
    (void) flags;

    try {
        if (graph != nullptr) {
            rememberCallbackError(context, "Named graphs are not supported by the Turtle reader");
            return SERD_ERR_BAD_ARG;
        }

        const auto rdf_subject = resourceNode(context, *subject, "subject", true);
        const auto rdf_predicate = resourceNode(context, *predicate, "predicate", false);
        if (!rdf_subject.has_value() || !rdf_predicate.has_value()) {
            return SERD_ERR_BAD_ARG;
        }

        RDFNode rdf_object;
        if (object->type == SERD_LITERAL) {
            if (object_language != nullptr) {
                rememberCallbackError(context, "Language-tagged RDF literals are not supported: '" +
                                                   nodeText(*object) + "'");
                return SERD_ERR_BAD_ARG;
            }

            std::optional<std::string> datatype;
            if (object_datatype != nullptr) {
                datatype = expandedIri(context, *object_datatype, "datatype");
                if (!datatype.has_value()) {
                    return SERD_ERR_BAD_ARG;
                }
            }
            rdf_object = RDFNode::literal(nodeText(*object), std::move(datatype));
        } else {
            const auto resource = resourceNode(context, *object, "object", true);
            if (!resource.has_value()) {
                return SERD_ERR_BAD_ARG;
            }
            rdf_object = *resource;
        }

        context.triples.push_back({*rdf_subject, *rdf_predicate, std::move(rdf_object)});
        return SERD_SUCCESS;
    } catch (const std::exception& error) {
        rememberCallbackError(context, error.what());
    } catch (...) {
        rememberCallbackError(context, "Unknown error in Turtle statement callback");
    }

    return SERD_ERR_UNKNOWN;
}

SerdStatus onError(void* handle, const SerdError* error) noexcept {
    auto& context = *static_cast<ParseContext*>(handle);

    try {
        char detail[1024] = {};
        va_list arguments;
        va_copy(arguments, *error->args);
        std::vsnprintf(detail, sizeof(detail), error->fmt, arguments);
        va_end(arguments);

        std::string message = "Turtle parse error";
        if (error->line != 0U) {
            message += " at line " + std::to_string(error->line);
            if (error->col != 0U) {
                message += ", column " + std::to_string(error->col);
            }
        }
        message += ": ";
        message += detail;
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        context.syntax_error = std::move(message);
    } catch (...) {
        // Preserve the parser status even if formatting the diagnostic fails.
    }

    return SERD_SUCCESS;
}

}  // namespace

std::vector<RDFTriple> SerdRdfReader::read(std::string_view turtle) const {
    using Environment = std::unique_ptr<SerdEnv, decltype(&serd_env_free)>;
    using Reader = std::unique_ptr<SerdReader, decltype(&serd_reader_free)>;

    if (turtle.find('\0') != std::string_view::npos) {
        throw std::runtime_error("Turtle input contains an embedded null byte");
    }

    Environment environment(serd_env_new(nullptr), serd_env_free);
    if (!environment) {
        throw std::runtime_error("Unable to create Serd environment");
    }

    ParseContext context;
    context.environment = environment.get();

    Reader reader(
        serd_reader_new(SERD_TURTLE, &context, nullptr, onBase, onPrefix, onStatement, nullptr),
        serd_reader_free);
    if (!reader) {
        throw std::runtime_error("Unable to create Serd Turtle reader");
    }

    serd_reader_set_strict(reader.get(), true);
    serd_reader_set_error_sink(reader.get(), onError, &context);

    const std::string input(turtle);
    const SerdStatus status =
        serd_reader_read_string(reader.get(), reinterpret_cast<const uint8_t*>(input.c_str()));

    if (!context.callback_error.empty()) {
        throw std::runtime_error(context.callback_error);
    }
    if (status != SERD_SUCCESS) {
        if (!context.syntax_error.empty()) {
            throw std::runtime_error(context.syntax_error);
        }

        throw std::runtime_error("Unable to parse Turtle: " +
                                 std::string(reinterpret_cast<const char*>(serd_strerror(status))));
    }

    return context.triples;
}

}  // namespace rdf_mapper
