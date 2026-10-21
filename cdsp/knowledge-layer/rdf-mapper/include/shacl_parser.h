#ifndef RDF_MAPPER_SHACL_PARSER_H
#define RDF_MAPPER_SHACL_PARSER_H

/**
 * @file shacl_parser.h
 * @brief Extracts the SHACL/VSS binding subset consumed by the RDF mapper.
 */

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

namespace rdf {

/// Type predicate used to discover explicitly declared sh:NodeShape resources.
inline constexpr std::string_view type = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";

}  // namespace rdf

namespace shacl {

/// Direct property predicate; complex SHACL paths are outside the decoded subset.
inline constexpr std::string_view path = "http://www.w3.org/ns/shacl#path";
/// Optional datatype IRI for a literal property binding.
inline constexpr std::string_view datatype = "http://www.w3.org/ns/shacl#datatype";
/// Exported vocabulary constant; sh:name is not stored in the decoded shape structures.
inline constexpr std::string_view name = "http://www.w3.org/ns/shacl#name";
/// Constant object binding, retaining the RDF term category and datatype.
inline constexpr std::string_view has_value = "http://www.w3.org/ns/shacl#hasValue";
/// Reference to another explicitly declared node shape in the same parsed input.
inline constexpr std::string_view node = "http://www.w3.org/ns/shacl#node";
/// Class identifying shapes eligible for the two-pass reference index.
inline constexpr std::string_view node_shape = "http://www.w3.org/ns/shacl#NodeShape";
/// Required target class of a decoded node shape.
inline constexpr std::string_view target_class = "http://www.w3.org/ns/shacl#targetClass";
/// Link from a node shape to an IRI or blank-node property shape.
inline constexpr std::string_view property = "http://www.w3.org/ns/shacl#property";
}  // namespace shacl

namespace vss {

/// Optional literal signal key preserved for exact mapping lookup.
inline constexpr std::string_view path = "https://covesa.org/vss/latest/model#path";
/// Optional literal property binding selector, retaining only its lexical value.
inline constexpr std::string_view value = "https://covesa.org/vss/latest/model#value";

}  // namespace vss

struct NodeShape;

/// Owning handle to a shape; retain the returned graph to keep its weak references usable.
using NodeShapePtr = std::shared_ptr<NodeShape>;

/**
 * @brief Mapper-facing property bindings extracted from one sh:property resource.
 *
 * This representation omits cardinality, node-kind, class, and other SHACL
 * validation constraints. Selector meaning and binding compatibility are checked
 * by the mapper, not by this parser.
 */
struct PropertyShape {
    /// Required direct IRI predicate; complex property paths are rejected.
    RDFNode sh_path;
    /// Optional non-owning reference; its target must be typed sh:NodeShape in the input.
    std::weak_ptr<NodeShape> sh_node;
    /// Optional constant object; any term representable by the reader is retained.
    std::optional<RDFNode> sh_has_value;
    /// Optional datatype IRI; no literal-value validation is performed here.
    std::optional<RDFNode> sh_datatype;
    /// Optional vss:value literal text; its datatype is not retained in this field.
    std::optional<std::string> vss_value;
};

/// Mapper-facing bindings of one explicitly declared node shape, without SHACL validation state.
struct NodeShape {
    /// Required target-class IRI.
    RDFNode sh_target_class;
    /// Optional vss:path literal; lexical value and optional datatype are retained.
    std::optional<RDFNode> vss_path;
    /// Property shapes in the order of distinct sh:property links in the parsed graph.
    std::vector<PropertyShape> sh_properties;
};

/**
 * @brief Decodes a restricted SHACL/VSS binding graph from Turtle.
 *
 * Only explicitly typed sh:NodeShape resources are indexed. A first pass allocates
 * all owning shape handles; a second pass populates them, permitting forward and
 * cyclic sh:node references. This parser extracts bindings, not a SHACL validator.
 * It requires sh:targetClass and direct IRI sh:path values and ignores constraints
 * not represented above. Singleton fields reject multiple distinct RDF objects.
 * Calls on the same parser instance require exclusive access.
 */
class SHACLParser {
   public:
    /**
     * @brief Reads Turtle and returns an owning handle for every declared node shape.
     * @param turtle Borrowed input text, copied/read synchronously during this call.
     * @return Shapes in first-occurrence order of their RDF type declarations.
     * @throws std::runtime_error On reader failure, missing required fields,
     *         wrong term categories, conflicting singleton fields, or unresolved sh:node links.
     * @note Retain all returned handles when following weak sh:node links. Parser
     *       reuse builds new handles and does not rewire previously returned graphs.
     *       RDF reading happens before the internal index is reset; a failure while
     *       decoding shapes can leave the internal index partially populated.
     */
    std::vector<NodeShapePtr> parse(std::string_view turtle);

   private:
    /// Orders map keys by the same type/value/datatype fields used for node equality.
    struct RDFNodeLess {
        /// Lexicographic comparison without prefix expansion or datatype normalization.
        bool operator()(const RDFNode& left, const RDFNode& right) const;
    };

    /// Extracts one property's supported bindings and resolves weak references via the index.
    PropertyShape parsePropertyShape(const RDFNode& property_node, const RDFGraph& graph);

    /// Requires a target class and appends the distinct linked property shapes.
    NodeShape parseNodeShape(const RDFNode& node_shape_iri, const RDFGraph& graph);

    /// Strong ownership of the latest allocated graph; weak links avoid reference cycles.
    std::map<RDFNode, NodeShapePtr, RDFNodeLess> node_shapes_map_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SHACL_PARSER_H
