#include "serd_rdf_writer.h"

#include <serd/serd.h>

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace rdf_mapper {
namespace {

constexpr std::string_view kXsd = "http://www.w3.org/2001/XMLSchema#";

/// Outlives both C handles, including any flush performed by writer destruction.
struct WriteContext {
    std::string output;
    std::exception_ptr sink_error;
    bool serd_error = false;
    char diagnostic[1024]{};
};

/// The C sink cannot throw, and Serd status alone does not report all short writes.
std::size_t appendOutput(const void* bytes, std::size_t size, void* handle) noexcept {
    auto& context = *static_cast<WriteContext*>(handle);
    if (context.sink_error) {
        return 0;
    }
    try {
        context.output.append(static_cast<const char*>(bytes), size);
        return size;
    } catch (...) {
        context.sink_error = std::current_exception();
        return 0;
    }
}

/// Keep the first diagnostic without allocating or throwing across the C boundary.
SerdStatus rememberSerdError(void* handle, const SerdError* error) noexcept {
    auto& context = *static_cast<WriteContext*>(handle);
    if (!context.serd_error) {
        context.serd_error = true;
        va_list arguments;
        va_copy(arguments, *error->args);
        std::vsnprintf(context.diagnostic, sizeof(context.diagnostic), error->fmt, arguments);
        va_end(arguments);
    }
    return SERD_SUCCESS;
}

void requireSuccess(SerdStatus status, const WriteContext& context) {
    if (context.sink_error) {
        std::rethrow_exception(context.sink_error);
    }
    if (context.serd_error) {
        throw std::runtime_error(std::string("Serd RDF writer: ") + context.diagnostic);
    }
    if (status != SERD_SUCCESS) {
        throw std::runtime_error("Serd RDF writer: " +
                                 std::string(reinterpret_cast<const char*>(serd_strerror(status))));
    }
}

/// Reject invalid byte sequences rather than letting Serd replace or truncate data.
void validateText(std::string_view text, bool is_iri = false) {
    const auto query = is_iri ? text.find('?') : std::string_view::npos;
    const auto fragment = is_iri ? text.find('#') : std::string_view::npos;
    for (std::size_t index = 0; index < text.size();) {
        const auto character_start = index;
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first == 0) {
            throw std::invalid_argument("RDF text contains an embedded NUL byte");
        }
        if (first < 0x80) {
            continue;
        }
        unsigned code_point;
        unsigned continuation_count;
        unsigned minimum;
        if (first >= 0xC2 && first <= 0xDF) {
            code_point = first & 0x1F;
            continuation_count = 1;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            code_point = first & 0x0F;
            continuation_count = 2;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            code_point = first & 0x07;
            continuation_count = 3;
            minimum = 0x10000;
        } else {
            throw std::invalid_argument("RDF text is not well-formed UTF-8");
        }
        if (continuation_count > text.size() - index) {
            throw std::invalid_argument("RDF text contains truncated UTF-8");
        }
        for (unsigned count = 0; count < continuation_count; ++count) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xC0) != 0x80) {
                throw std::invalid_argument("RDF text is not well-formed UTF-8");
            }
            code_point = (code_point << 6) | (byte & 0x3F);
        }
        if (code_point < minimum || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            throw std::invalid_argument("RDF text contains an invalid Unicode scalar value");
        }
        if (is_iri) {
            // RFC 3987 ucschar; Unicode private-use characters belong only in queries.
            const bool ucschar = (code_point >= 0xA0 && code_point <= 0xD7FF) ||
                                 (code_point >= 0xF900 && code_point <= 0xFDCF) ||
                                 (code_point >= 0xFDF0 && code_point <= 0xFFEF) ||
                                 (code_point >= 0x10000 && code_point <= 0xDFFFD &&
                                  (code_point & 0xFFFF) <= 0xFFFD) ||
                                 (code_point >= 0xE1000 && code_point <= 0xEFFFD);
            const bool private_character = (code_point >= 0xE000 && code_point <= 0xF8FF) ||
                                           (code_point >= 0xF0000 && code_point <= 0x10FFFD &&
                                            (code_point & 0xFFFF) <= 0xFFFD);
            const bool in_query =
                query != std::string_view::npos && character_start > query &&
                (fragment == std::string_view::npos || character_start < fragment);
            if (!ucschar && !(private_character && in_query)) {
                throw std::invalid_argument("RDF IRI contains a disallowed Unicode character");
            }
        }
    }
}

/// Check serialization safety/absoluteness, not scheme-specific endpoint or host semantics.
void validateIri(const std::string& iri) {
    validateText(iri, true);
    if (!serd_uri_string_has_scheme(reinterpret_cast<const uint8_t*>(iri.c_str()))) {
        throw std::invalid_argument("RDF IRI must be absolute: " + iri);
    }
    SerdURI components{};
    serd_uri_parse(reinterpret_cast<const uint8_t*>(iri.c_str()), &components);
    const auto authority_start =
        components.authority.buf
            ? static_cast<std::size_t>(components.authority.buf -
                                       reinterpret_cast<const uint8_t*>(iri.c_str()))
            : std::string::npos;
    const auto fragment = iri.find('#');
    if (fragment != std::string::npos && iri.find('#', fragment + 1) != std::string::npos) {
        throw std::invalid_argument("RDF IRI contains more than one fragment delimiter: " + iri);
    }
    for (std::size_t index = 0; index < iri.size(); ++index) {
        const auto byte = static_cast<unsigned char>(iri[index]);
        if (byte <= 0x20 || byte == 0x7F ||
            std::string_view("<>\"{}|^`\\").find(iri[index]) != std::string_view::npos) {
            throw std::invalid_argument("RDF IRI contains an unencoded character: " + iri);
        }
        if ((byte == '[' || byte == ']') &&
            (authority_start == std::string::npos || index < authority_start ||
             index - authority_start >= components.authority.len)) {
            throw std::invalid_argument(
                "RDF IRI brackets are reserved for authority IP literals: " + iri);
        }
        if (byte == '%') {
            if (index + 2 >= iri.size() ||
                std::string_view(iri)
                        .substr(index + 1, 2)
                        .find_first_not_of("0123456789ABCDEFabcdef") != std::string_view::npos) {
                throw std::invalid_argument("RDF IRI contains an invalid percent escape: " + iri);
            }
            index += 2;
        }
    }
}

/// An explicit portable subset avoids unchecked Turtle prefix/blank-label emission.
void validateIdentifier(std::string_view text, bool is_prefix) {
    constexpr std::string_view letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    constexpr std::string_view blank_start =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
    constexpr std::string_view label_characters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-";
    if ((text.empty() && !is_prefix) ||
        (!text.empty() &&
         ((is_prefix ? letters : blank_start).find(text.front()) == std::string_view::npos ||
          text.find_first_not_of(label_characters) != std::string_view::npos ||
          text.back() == '.'))) {
        throw std::invalid_argument(is_prefix ? "Invalid RDF prefix name"
                                              : "Invalid RDF blank label");
    }
}

std::string expandCurie(const std::string& curie,
                        const std::map<std::string, std::string>& prefixes) {
    validateText(curie);
    const auto colon = curie.find(':');
    const auto prefix = prefixes.find(curie.substr(0, colon));
    if (colon == std::string::npos || prefix == prefixes.end()) {
        throw std::invalid_argument("RDF CURIE has no registered prefix: " + curie);
    }
    std::string expanded = prefix->second + curie.substr(colon + 1);
    validateIri(expanded);
    return expanded;
}

/// Values returned here own the bytes borrowed by Serd nodes in one statement call.
RDFNode normalizedNode(const RDFNode& node, const std::map<std::string, std::string>& prefixes,
                       bool allow_literal, bool allow_blank) {
    if (node.type != RDFNodeType::Literal && node.datatype.has_value()) {
        throw std::invalid_argument("Only RDF literal nodes may have a datatype");
    }
    switch (node.type) {
        case RDFNodeType::Iri:
            validateIri(node.value);
            return node;
        case RDFNodeType::Curie:
            return RDFNode::iri(expandCurie(node.value, prefixes));
        case RDFNodeType::BlankNode:
            if (allow_blank) {
                validateIdentifier(node.value, false);
                return node;
            }
            break;
        case RDFNodeType::Literal:
            if (allow_literal) {
                validateText(node.value);
                RDFNode result = node;
                if (result.datatype.has_value()) {
                    const auto colon = result.datatype->find(':');
                    if (colon != std::string::npos &&
                        prefixes.find(result.datatype->substr(0, colon)) != prefixes.end()) {
                        result.datatype = expandCurie(*result.datatype, prefixes);
                    } else {
                        validateIri(*result.datatype);
                    }
                }
                return result;
            }
            break;
    }
    throw std::invalid_argument("Invalid RDF node type for statement position");
}

SerdNode borrowedNode(const RDFNode& node) {
    const auto type = node.type == RDFNodeType::Literal     ? SERD_LITERAL
                      : node.type == RDFNodeType::BlankNode ? SERD_BLANK
                                                            : SERD_URI;
    return serd_node_from_string(type, reinterpret_cast<const uint8_t*>(node.value.c_str()));
}

/// These full-IRI datatypes trigger unsafe bare-token shortcuts in Serd 0.x Turtle/TriG.
bool requiresDatatypeCurie(const RDFNode& object) {
    return object.datatype.has_value() && (*object.datatype == std::string(kXsd) + "boolean" ||
                                           *object.datatype == std::string(kXsd) + "integer" ||
                                           *object.datatype == std::string(kXsd) + "decimal");
}

}  // namespace

SerdRdfWriter::SerdRdfWriter(std::map<std::string, std::string> prefixes) {
    for (const auto& prefix : prefixes) {
        setPrefix(prefix.first, prefix.second);
    }
}

void SerdRdfWriter::setPrefix(const std::string& prefix, const std::string& iri) {
    validateIdentifier(prefix, true);
    validateIri(iri);
    prefixes_.insert_or_assign(prefix, iri);
}

std::string SerdRdfWriter::write(const std::vector<RDFTriple>& triples, RDFFormat format) const {
    SerdSyntax syntax;
    switch (format) {
        case RDFFormat::Turtle:
            syntax = SERD_TURTLE;
            break;
        case RDFFormat::NTriples:
            syntax = SERD_NTRIPLES;
            break;
        case RDFFormat::NQuads:
            syntax = SERD_NQUADS;
            break;
        case RDFFormat::TriG:
            syntax = SERD_TRIG;
            break;
        default:
            throw std::invalid_argument("Unsupported RDF output format");
    }

    // Validate before touching Serd. In particular, its C API accepts blank predicates
    // and does not reliably reject unbound CURIEs or malformed UTF-8 in every syntax.
    std::vector<RDFTriple> normalized;
    normalized.reserve(triples.size());
    bool needs_xsd_prefix = false;
    for (const auto& triple : triples) {
        normalized.push_back({normalizedNode(triple.subject, prefixes_, false, true),
                              normalizedNode(triple.predicate, prefixes_, false, false),
                              normalizedNode(triple.object, prefixes_, true, true)});
        needs_xsd_prefix |= requiresDatatypeCurie(normalized.back().object);
    }

    std::map<std::string, std::string> output_prefixes = prefixes_;
    std::string xsd_prefix;
    const bool terse = syntax == SERD_TURTLE || syntax == SERD_TRIG;
    if (terse && needs_xsd_prefix) {
        bool found = false;
        for (const auto& prefix : output_prefixes) {
            if (prefix.second == kXsd) {
                xsd_prefix = prefix.first;
                found = true;
                break;
            }
        }
        if (!found) {
            xsd_prefix = "xsd";
            for (std::size_t suffix = 1; output_prefixes.count(xsd_prefix); ++suffix) {
                xsd_prefix = "xsd" + std::to_string(suffix);
            }
            output_prefixes.emplace(xsd_prefix, kXsd);
        }
    }

    using Environment = std::unique_ptr<SerdEnv, decltype(&serd_env_free)>;
    using Writer = std::unique_ptr<SerdWriter, decltype(&serd_writer_free)>;
    WriteContext context;
    Environment environment(serd_env_new(nullptr), serd_env_free);
    if (!environment) {
        throw std::runtime_error("Unable to create Serd writer environment");
    }
    Writer writer(serd_writer_new(syntax, SERD_STYLE_CURIED, environment.get(), nullptr,
                                  appendOutput, &context),
                  serd_writer_free);
    if (!writer) {
        throw std::runtime_error("Unable to create Serd RDF writer");
    }
    serd_writer_set_error_sink(writer.get(), rememberSerdError, &context);
    for (const auto& prefix : output_prefixes) {
        const SerdNode name = serd_node_from_string(
            SERD_LITERAL, reinterpret_cast<const uint8_t*>(prefix.first.c_str()));
        const SerdNode iri = serd_node_from_string(
            SERD_URI, reinterpret_cast<const uint8_t*>(prefix.second.c_str()));
        requireSuccess(serd_writer_set_prefix(writer.get(), &name, &iri), context);
    }

    for (const auto& triple : normalized) {
        const SerdNode subject = borrowedNode(triple.subject);
        const SerdNode predicate = borrowedNode(triple.predicate);
        const SerdNode object = borrowedNode(triple.object);
        SerdNode datatype = SERD_NODE_NULL;
        std::string datatype_curie;
        if (triple.object.datatype.has_value()) {
            if (terse && requiresDatatypeCurie(triple.object)) {
                // A CURIE bypasses Serd's bare boolean/integer/decimal shortcut and
                // preserves both lexical text and datatype, including boolean "1".
                datatype_curie = xsd_prefix + ':' + triple.object.datatype->substr(kXsd.size());
                datatype = serd_node_from_string(
                    SERD_CURIE, reinterpret_cast<const uint8_t*>(datatype_curie.c_str()));
            } else {
                datatype = serd_node_from_string(
                    SERD_URI, reinterpret_cast<const uint8_t*>(triple.object.datatype->c_str()));
            }
        }
        // RDFTriple has no graph component. N-Quads/TriG therefore use the default graph.
        requireSuccess(
            serd_writer_write_statement(writer.get(), 0, nullptr, &subject, &predicate, &object,
                                        datatype.buf ? &datatype : nullptr, nullptr),
            context);
    }
    requireSuccess(serd_writer_finish(writer.get()), context);
    writer.reset();
    requireSuccess(SERD_SUCCESS, context);  // Destruction may flush through the same sink.
    return std::move(context.output);
}

}  // namespace rdf_mapper
