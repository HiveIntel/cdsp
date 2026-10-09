#include "rdf_model.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace rdf_mapper {

RDFNode RDFNode::curie(const std::string& value) {
    return {RDFNodeType::Curie, value, std::nullopt};
}

RDFNode RDFNode::iri(const std::string& value) {
    return {RDFNodeType::Iri, value, std::nullopt};
}

RDFNode RDFNode::blankNode(const std::string& identifier) {
    return {RDFNodeType::BlankNode, identifier, std::nullopt};
}

RDFNode RDFNode::literal(const std::string& value,
                         std::optional<std::string> datatype) {
    return {RDFNodeType::Literal, value, std::move(datatype)};
}

bool operator==(const RDFNode& left, const RDFNode& right) {
    return left.type == right.type && left.value == right.value &&
           left.datatype == right.datatype;
}

bool operator!=(const RDFNode& left, const RDFNode& right) {
    return !(left == right);
}

bool operator==(const RDFTriple& left, const RDFTriple& right) {
    return left.subject == right.subject && left.predicate == right.predicate &&
           left.object == right.object;
}

bool operator!=(const RDFTriple& left, const RDFTriple& right) {
    return !(left == right);
}

namespace {

bool hasPredicateIri(const RDFTriple& triple,
                     std::string_view predicate_iri) {
    return triple.predicate.type == RDFNodeType::Iri &&
           std::string_view(triple.predicate.value) == predicate_iri;
}

void appendUnique(std::vector<RDFNode>& nodes, const RDFNode& node) {
    if (std::find(nodes.cbegin(), nodes.cend(), node) == nodes.cend()) {
        nodes.push_back(node);
    }
}

}  // namespace

RDFGraph::RDFGraph(std::vector<RDFTriple> triples)
    : triples_(std::move(triples)) {}

std::vector<RDFNode> RDFGraph::objects(
    const RDFNode& subject, std::string_view predicate_iri) const {
    std::vector<RDFNode> matches;
    for (const RDFTriple& triple : triples_) {
        if (triple.subject == subject &&
            hasPredicateIri(triple, predicate_iri)) {
            appendUnique(matches, triple.object);
        }
    }
    return matches;
}

std::optional<RDFNode> RDFGraph::object(
    const RDFNode& subject, std::string_view predicate_iri) const {
    std::vector<RDFNode> matches = objects(subject, predicate_iri);
    if (matches.empty()) {
        return std::nullopt;
    }
    if (matches.size() > 1U) {
        throw std::runtime_error(
            "Expected at most one RDF object for predicate '" +
            std::string(predicate_iri) + "', found " +
            std::to_string(matches.size()));
    }
    return std::move(matches.front());
}

std::vector<RDFNode> RDFGraph::subjects(
    std::string_view predicate_iri, const RDFNode& object) const {
    std::vector<RDFNode> matches;
    for (const RDFTriple& triple : triples_) {
        if (hasPredicateIri(triple, predicate_iri) &&
            triple.object == object) {
            appendUnique(matches, triple.subject);
        }
    }
    return matches;
}

}  // namespace rdf_mapper
