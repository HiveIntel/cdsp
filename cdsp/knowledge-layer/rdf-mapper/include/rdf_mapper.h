#ifndef RDF_MAPPER_RDF_MAPPER_H
#define RDF_MAPPER_RDF_MAPPER_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "il_message_dto.h"
#include "rdf_model.h"
#include "rdf_writer.h"
#include "shacl_parser.h"

namespace rdf_mapper {

class RDFMapper {
   public:

    RDFMapper(const std::filesystem::path& shape_directory);

    std::string signalToTurtle(const il_message::DataMessageDTO& message) const;

    std::string vehicleToTurtle(vehicle_id) const;

};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_MAPPER_H
