#ifndef RDF_MAPPER_RDF_WRITER_H
#define RDF_MAPPER_RDF_WRITER_H

#include <map>
#include <string>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

class RDFWriter {
   public:
    explicit RDFWriter(std::map<std::string, std::string> prefixes = {});

    void setPrefix(const std::string& prefix, const std::string& uri);

    std::string write(const std::vector<RDFTriple>& triples,
                      RDFFormat format = RDFFormat::Turtle) const;

   private:
    std::map<std::string, std::string> prefixes_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_WRITER_H
