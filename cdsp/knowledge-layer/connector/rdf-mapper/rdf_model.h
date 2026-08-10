#ifndef RDF_MAPPER_RDF_MODEL_H
#define RDF_MAPPER_RDF_MODEL_H

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rdf_mapper {

enum class RdfFormat {
    Turtle,
    NTriples,
    NQuads,
    TriG
};

enum class RdfNodeType {
    Curie,
    Iri,
    Literal
};

struct RdfNode {
    RdfNodeType type = RdfNodeType::Curie;
    std::string value;
    std::optional<std::string> datatype;

    static RdfNode curie(const std::string& value);
    static RdfNode iri(const std::string& value);
    static RdfNode literal(const std::string& value,
                           std::optional<std::string> datatype = std::nullopt);
};

struct RdfTriple {
    RdfNode subject;
    RdfNode predicate;
    RdfNode object;
};

using PrefixMap = std::map<std::string, std::string>;

enum class PropertyValueKind {
    ConstantIri,
    InputLiteral,
    GeneratedNodeIri
};

struct PropertyShapeDef {
    std::string name;
    std::string path_iri;
    PropertyValueKind value_kind = PropertyValueKind::InputLiteral;
    std::optional<std::string> datatype_iri;
    std::optional<std::string> constant_iri;
    std::optional<std::string> node_shape_iri;
};

struct NodeShapeDef {
    std::string shape_iri;
    std::string name;
    std::optional<std::string> class_iri;
    std::optional<std::string> target_class_iri;
    std::string iri_prefix;
    std::vector<PropertyShapeDef> properties;
};

struct ShapeRegistry {
    PrefixMap prefixes;
    std::unordered_map<std::string, NodeShapeDef> node_shapes_by_iri;
    std::unordered_map<std::string, std::string> target_shapes_by_name;
};

struct InputMessage {
    std::unordered_map<std::string, std::string> values;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_MODEL_H
