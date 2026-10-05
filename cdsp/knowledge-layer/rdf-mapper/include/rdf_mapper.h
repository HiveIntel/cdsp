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
    RDFMapper(
        const std::vector<std::filesystem::path>& shape_paths,
        RDFFormat format = RDFFormat::Turtle);

    RDFMapper(
        const std::filesystem::path& shape_directory,
        RDFFormat format = RDFFormat::Turtle);

    std::vector<RDFTriple> signalToTriples(
        const il_message::DataMessageDTO& message) const;

    std::string signalToRDF(const il_message::DataMessageDTO& message) const;

   private:
    void loadSignalShapes(
        const std::vector<std::filesystem::path>& shape_paths);

    RDFWriter writer_;
    std::unordered_map<std::string, NodeShape> signal_shapes_by_path_;
    RDFFormat format_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_MAPPER_H
