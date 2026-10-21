#include "shacl_parser.h"

/**
 * @file shacl_parser.cpp
 * @brief Two-pass extraction of mapper bindings from an owned RDF graph.
 */

#include <stdexcept>

#include "serd_rdf_reader.h"

namespace rdf_mapper {

namespace {

/// Copies an IRI-valued binding or reports the predicate whose term category is invalid.
RDFNode requireIri(const RDFNode& node, std::string_view predicate) {
    if (node.type != RDFNodeType::Iri) {
        throw std::runtime_error("SHACL predicate '" + std::string(predicate) +
                                 "' must have an IRI value");
    }
    return node;
}

/// Copies a literal binding without interpreting its lexical value or datatype.
RDFNode requireLiteral(const RDFNode& node, std::string_view predicate) {
    if (node.type != RDFNodeType::Literal) {
        throw std::runtime_error("SHACL predicate '" + std::string(predicate) +
                                 "' must have a literal value");
    }
    return node;
}

/// Reads a singleton literal binding; graph.object also rejects distinct competing values.
std::optional<RDFNode> optionalLiteral(const RDFNode& subject, std::string_view predicate,
                                       const RDFGraph& graph) {
    const std::optional<RDFNode> object = graph.object(subject, predicate);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return requireLiteral(*object, predicate);
}

/// Reads a singleton IRI binding, distinguishing an absent predicate from an invalid value.
std::optional<RDFNode> optionalIri(const RDFNode& subject, std::string_view predicate,
                                   const RDFGraph& graph) {
    const std::optional<RDFNode> object = graph.object(subject, predicate);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return requireIri(*object, predicate);
}

/// Reads only a literal's owned lexical value; its optional datatype is intentionally discarded.
std::optional<std::string> optionalLiteralValue(const RDFNode& subject, std::string_view predicate,
                                                const RDFGraph& graph) {
    const std::optional<RDFNode> object = optionalLiteral(subject, predicate, graph);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return object->value;
}

}  // namespace

bool SHACLParser::RDFNodeLess::operator()(const RDFNode& left, const RDFNode& right) const {
    if (left.type != right.type) {
        return left.type < right.type;
    }
    if (left.value != right.value) {
        return left.value < right.value;
    }
    return left.datatype < right.datatype;
}

std::vector<NodeShapePtr> SHACLParser::parse(std::string_view turtle) {
    // Complete RDF reading first: reader failures preserve the previous parser index.
    const RDFGraph graph(SerdRdfReader{}.read(turtle));

    const std::vector<RDFNode> node_shape_iris =
        graph.subjects(rdf::type, RDFNode::iri(std::string(shacl::node_shape)));

    // Pass one establishes stable identities before any sh:node reference is resolved.
    node_shapes_map_.clear();
    for (const RDFNode& node_shape_iri : node_shape_iris) {
        node_shapes_map_.emplace(node_shape_iri, std::make_shared<NodeShape>());
    }

    std::vector<NodeShapePtr> node_shapes;
    node_shapes.reserve(node_shape_iris.size());

    // Pass two fills the existing objects; replacing their shared_ptrs would break weak links.
    for (const RDFNode& node_shape_iri : node_shape_iris) {
        NodeShapePtr node_shape = node_shapes_map_.at(node_shape_iri);
        *node_shape = parseNodeShape(node_shape_iri, graph);
        node_shapes.push_back(std::move(node_shape));
    }

    return node_shapes;
}

PropertyShape SHACLParser::parsePropertyShape(const RDFNode& property_node, const RDFGraph& graph) {
    const std::optional<RDFNode> path_node = graph.object(property_node, shacl::path);

    if (!path_node.has_value()) {
        throw std::runtime_error("PropertyShape is missing required sh:path");
    }

    PropertyShape property{};
    property.sh_path = requireIri(*path_node, shacl::path);
    property.vss_value = optionalLiteralValue(property_node, vss::value, graph);
    property.sh_has_value = graph.object(property_node, shacl::has_value);
    property.sh_datatype = optionalIri(property_node, shacl::datatype, graph);

    const std::optional<RDFNode> referenced_shape = graph.object(property_node, shacl::node);
    if (referenced_shape.has_value()) {
        if (referenced_shape->type != RDFNodeType::Iri &&
            referenced_shape->type != RDFNodeType::BlankNode) {
            throw std::runtime_error("SHACL predicate '" + std::string(shacl::node) +
                                     "' must reference an IRI or blank node");
        }

        const auto referenced_entry = node_shapes_map_.find(*referenced_shape);
        if (referenced_entry == node_shapes_map_.end()) {
            throw std::runtime_error("SHACL predicate '" + std::string(shacl::node) +
                                     "' references a node that is not typed as sh:NodeShape");
        }
        // Do not create a strong edge: legal cyclic references must not form ownership cycles.
        property.sh_node = referenced_entry->second;
    }

    return property;
}

NodeShape SHACLParser::parseNodeShape(const RDFNode& node_shape_iri, const RDFGraph& graph) {
    const std::optional<RDFNode> target_class = graph.object(node_shape_iri, shacl::target_class);

    if (!target_class.has_value()) {
        throw std::runtime_error("NodeShape is missing required sh:targetClass");
    }

    NodeShape shape{};
    shape.sh_target_class = requireIri(*target_class, shacl::target_class);
    shape.vss_path = optionalLiteral(node_shape_iri, vss::path, graph);

    for (const RDFNode& property_node : graph.objects(node_shape_iri, shacl::property)) {
        if (property_node.type != RDFNodeType::Iri &&
            property_node.type != RDFNodeType::BlankNode) {
            throw std::runtime_error("SHACL predicate '" + std::string(shacl::property) +
                                     "' must reference an IRI or blank node");
        }
        shape.sh_properties.push_back(parsePropertyShape(property_node, graph));
    }
    return shape;
}

}  // namespace rdf_mapper
