#include "rdf_model.h"

#include <utility>

namespace rdf_mapper {

RdfNode RdfNode::curie(const std::string& value) {
    return {RdfNodeType::Curie, value, std::nullopt};
}

RdfNode RdfNode::iri(const std::string& value) {
    return {RdfNodeType::Iri, value, std::nullopt};
}

RdfNode RdfNode::literal(const std::string& value, std::optional<std::string> datatype) {
    return {RdfNodeType::Literal, value, std::move(datatype)};
}

}  // namespace rdf_mapper
