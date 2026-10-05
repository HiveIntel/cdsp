#ifndef RDF_MAPPER_SHACL_PARSER_H
#define RDF_MAPPER_SHACL_PARSER_H

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

namespace rdf {

inline constexpr std::string_view type = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";

}  

namespace shacl {

inline constexpr std::string_view path = "http://www.w3.org/ns/shacl#path";
inline constexpr std::string_view datatype = "http://www.w3.org/ns/shacl#datatype";
inline constexpr std::string_view name = "http://www.w3.org/ns/shacl#name";
inline constexpr std::string_view class_ = "http://www.w3.org/ns/shacl#class";
inline constexpr std::string_view has_value = "http://www.w3.org/ns/shacl#hasValue";
inline constexpr std::string_view node = "http://www.w3.org/ns/shacl#node";
inline constexpr std::string_view node_shape = "http://www.w3.org/ns/shacl#NodeShape";
inline constexpr std::string_view target_class = "http://www.w3.org/ns/shacl#targetClass";
inline constexpr std::string_view property = "http://www.w3.org/ns/shacl#property";
}  // namespace shacl

namespace vss {
inline constexpr std::string_view path = "https://covesa.org/vss/latest/model#path";
inline constexpr std::string_view value ="https://covesa.org/vss/latest/model#value";
}  


struct PropertyShape {
    std::string sh_path;
    std::optional<std::string> vss_value;
    std::optional<std::string> sh_class;
    std::optional<std::string> sh_node;
    std::optional<std::string> sh_has_value;
    std::optional<std::string> sh_datatype;
};

struct NodeShape {
    std::string iri;
    std::string sh_targetClass;
    std::optional<std::string> vss_path;
    std::vector<PropertyShape> sh_property;
};


class SHACLParser {
   public:
    std::vector<NodeShape> parse(std::string_view turtle) const;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SHACL_PARSER_H
