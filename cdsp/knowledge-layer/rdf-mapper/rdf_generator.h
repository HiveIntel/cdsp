#ifndef RDF_MAPPER_RDF_GENERATOR_H
#define RDF_MAPPER_RDF_GENERATOR_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

class RdfGenerator {
   public:
    explicit RdfGenerator(const ShapeRegistry& registry);

    std::vector<RdfTriple> generate(const InputMessage& message,
                                    RdfFormat format = RdfFormat::Turtle) const;

   private:
    const ShapeRegistry& registry_;

    const NodeShapeDef& shapeForMessage(const InputMessage& message) const;

    std::string generateNode(const NodeShapeDef& shape,
                             const InputMessage& message,
                             std::unordered_map<std::string, std::string>& generated_nodes,
                             std::unordered_set<std::string>& visiting_nodes,
                             std::vector<RdfTriple>& triples) const;

    std::string requiredValue(const InputMessage& message, const std::string& field) const;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_GENERATOR_H
