#ifndef RDF_MAPPER_SHACL_SHAPE_PARSER_H
#define RDF_MAPPER_SHACL_SHAPE_PARSER_H

#include <filesystem>
#include <string>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

class ShaclShapeParser {
   public:
    ShapeRegistry loadFiles(const std::vector<std::filesystem::path>& paths) const;
    ShapeRegistry loadDirectory(const std::filesystem::path& directory) const;

   private:
    void parseFile(const std::filesystem::path& path, ShapeRegistry& registry) const;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SHACL_SHAPE_PARSER_H
