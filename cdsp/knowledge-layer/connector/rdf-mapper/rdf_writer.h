#ifndef RDF_MAPPER_RDF_WRITER_H
#define RDF_MAPPER_RDF_WRITER_H

#include <string>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

class RDFWriter {
   public:
    explicit RDFWriter(PrefixMap prefixes = {});

    void setPrefix(const std::string& prefix, const std::string& uri);
    
    std::string write(const std::vector<RdfTriple>& triples,
                      RdfFormat format = RdfFormat::Turtle) const;

   private:
    PrefixMap prefixes_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_WRITER_H
