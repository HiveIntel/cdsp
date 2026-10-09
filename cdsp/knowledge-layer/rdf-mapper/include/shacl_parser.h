#ifndef RDF_MAPPER_SHACL_PARSER_H
#define RDF_MAPPER_SHACL_PARSER_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

namespace rdf {

inline constexpr std::string_view type = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";

}  // namespace rdf

namespace shacl {

inline constexpr std::string_view path = "http://www.w3.org/ns/shacl#path";
inline constexpr std::string_view datatype = "http://www.w3.org/ns/shacl#datatype";
inline constexpr std::string_view name = "http://www.w3.org/ns/shacl#name";
inline constexpr std::string_view has_value = "http://www.w3.org/ns/shacl#hasValue";
inline constexpr std::string_view node = "http://www.w3.org/ns/shacl#node";
inline constexpr std::string_view node_shape = "http://www.w3.org/ns/shacl#NodeShape";
inline constexpr std::string_view target_class = "http://www.w3.org/ns/shacl#targetClass";
inline constexpr std::string_view property = "http://www.w3.org/ns/shacl#property";
}  // namespace shacl

namespace vss {

inline constexpr std::string_view path = "https://covesa.org/vss/latest/model#path";
inline constexpr std::string_view value = "https://covesa.org/vss/latest/model#value";

}  // namespace vss

struct NodeShape;

using NodeShapePtr = std::shared_ptr<NodeShape>;

struct PropertyShape {
    RDFNode sh_path;
    std::weak_ptr<NodeShape> sh_node;
    std::optional<RDFNode> sh_has_value;
    std::optional<RDFNode> sh_datatype;
    std::optional<std::string> vss_value;
};

struct NodeShape {
    RDFNode sh_target_class;
    std::optional<RDFNode> vss_path;
    std::vector<PropertyShape> sh_properties;
};

class SHACLParser {
   public:
    std::vector<NodeShapePtr> parse(std::string_view turtle);

   private:
    struct RDFNodeLess {
        bool operator()(const RDFNode& left, const RDFNode& right) const;
    };

    PropertyShape parsePropertyShape(const RDFNode& property_node, const RDFGraph& graph);

    NodeShape parseNodeShape(const RDFNode& node_shape_iri, const RDFGraph& graph);

    std::map<RDFNode, NodeShapePtr, RDFNodeLess> node_shapes_map_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SHACL_PARSER_H
