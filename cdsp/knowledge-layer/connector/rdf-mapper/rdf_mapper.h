#ifndef RDF_MAPPER_RDF_MAPPER_H
#define RDF_MAPPER_RDF_MAPPER_H

#include <filesystem>
#include <string>
#include <vector>

#include "rdf_generator.h"
#include "rdf_model.h"
#include "rdf_writer.h"

namespace rdf_mapper {

class RdfMapper {
   public:
    RdfMapper(const std::vector<std::filesystem::path>& shape_paths,
              std::vector<std::string> vehicle_signals,
              RdfFormat format = RdfFormat::Turtle);

    RdfMapper(const std::filesystem::path& shape_directory,
              std::vector<std::string> vehicle_signals,
              RdfFormat format = RdfFormat::Turtle);

    std::string mapMessageToRdf(const std::string& message) const;

   private:
    ShapeRegistry registry_;
    RdfGenerator generator_;
    RDFWriter writer_;
    RdfFormat format_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_MAPPER_H
