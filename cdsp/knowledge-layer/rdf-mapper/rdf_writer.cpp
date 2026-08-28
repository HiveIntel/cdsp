#include "rdf_writer.h"

#include <serd/serd.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace rdf_mapper {

namespace {

bool isAngleIri(const std::string& value) {
    return value.size() >= 2 && value.front() == '<' && value.back() == '>';
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool isAbsoluteIri(const std::string& value) {
    return startsWith(value, "http://") || startsWith(value, "https://") ||
           startsWith(value, "urn:");
}

std::string unwrapAngleIri(const std::string& value) {
    if (!isAngleIri(value)) {
        return value;
    }

    return value.substr(1, value.size() - 2);
}

SerdNode makeNode(SerdType type, const std::string& value) {
    return serd_node_from_string(type, reinterpret_cast<const uint8_t*>(value.c_str()));
}

SerdNode makeResourceNode(const RdfNode& node, std::string& storage) {
    if (node.type == RdfNodeType::Literal) {
        throw std::runtime_error("Literal cannot be used as an RDF resource node");
    }

    storage = unwrapAngleIri(node.value);
    const SerdType serd_type = node.type == RdfNodeType::Iri ? SERD_URI : SERD_CURIE;
    return makeNode(serd_type, storage);
}

SerdNode makeDatatypeNode(const std::string& datatype, std::string& storage) {
    storage = unwrapAngleIri(datatype);
    const SerdType serd_type = isAngleIri(datatype) || isAbsoluteIri(datatype) ? SERD_URI : SERD_CURIE;
    return makeNode(serd_type, storage);
}

size_t writeSerdOutputToString(const void* buffer, size_t length, void* stream) {
    auto* output = static_cast<std::string*>(stream);
    output->append(static_cast<const char*>(buffer), length);
    return length;
}

SerdSyntax toSerdSyntax(RdfFormat format) {
    switch (format) {
        case RdfFormat::Turtle:
            return SERD_TURTLE;
        case RdfFormat::NTriples:
            return SERD_NTRIPLES;
        case RdfFormat::NQuads:
            return SERD_NQUADS;
        case RdfFormat::TriG:
            return SERD_TRIG;
        default:
            throw std::runtime_error("Unsupported RDF writer format");
    }
}

}  // namespace

RDFWriter::RDFWriter(PrefixMap prefixes) : prefixes_(std::move(prefixes)) {}

void RDFWriter::setPrefix(const std::string& prefix, const std::string& uri) {
    if (prefix.empty()) {
        throw std::runtime_error("RDF prefix cannot be empty");
    }
    if (uri.empty()) {
        throw std::runtime_error("RDF prefix URI cannot be empty");
    }

    prefixes_[prefix] = uri;
}

std::string RDFWriter::write(const std::vector<RdfTriple>& triples, RdfFormat format) const {
    if (triples.empty()) {
        //TODO return empty string
        throw std::runtime_error("RDF writer input does not contain triples");
    }

    std::unique_ptr<SerdEnv, decltype(&serd_env_free)> serd_env(serd_env_new(nullptr),
                                                                serd_env_free);

    if (!serd_env) {
        throw std::runtime_error("Failed to create Serd environment");
    }

    std::string output;
    std::unique_ptr<SerdWriter, decltype(&serd_writer_free)> serd_writer(
        serd_writer_new(toSerdSyntax(format), SERD_STYLE_ABBREVIATED, serd_env.get(), nullptr,
                        writeSerdOutputToString, &output),
        serd_writer_free);
    if (!serd_writer) {
        throw std::runtime_error("Failed to create Serd writer");
    }

    for (const auto& [prefix, uri] : prefixes_) {
        SerdNode prefix_node = makeNode(SERD_CURIE, prefix);
        SerdNode uri_node = makeNode(SERD_URI, uri);
        serd_writer_set_prefix(serd_writer.get(), &prefix_node, &uri_node);
    }

    for (const auto& triple : triples) {
        std::string subject_storage;
        std::string predicate_storage;
        std::string object_storage;
        std::string datatype_storage;

        SerdNode subject_node = makeResourceNode(triple.subject, subject_storage);
        SerdNode predicate_node = makeResourceNode(triple.predicate, predicate_storage);
        SerdNode object_node = triple.object.type == RdfNodeType::Literal
                                   ? makeNode(SERD_LITERAL, triple.object.value)
                                   : makeResourceNode(triple.object, object_storage);

        const SerdNode* datatype_node_ptr = nullptr;
        SerdNode datatype_node{};
        if (triple.object.datatype.has_value()) {
            datatype_node = makeDatatypeNode(triple.object.datatype.value(), datatype_storage);
            datatype_node_ptr = &datatype_node;
        }

        const SerdStatus status = serd_writer_write_statement(
            serd_writer.get(), 0, nullptr, &subject_node, &predicate_node, &object_node,
            datatype_node_ptr, nullptr);
        if (status != SERD_SUCCESS) {
            throw std::runtime_error("Serd failed to write RDF statement");
        }
    }

    serd_writer_finish(serd_writer.get());
    return output;
}

}  // namespace rdf_mapper
