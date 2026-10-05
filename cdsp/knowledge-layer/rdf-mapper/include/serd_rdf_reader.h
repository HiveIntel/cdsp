#ifndef RDF_MAPPER_SERD_RDF_READER_H
#define RDF_MAPPER_SERD_RDF_READER_H

#include <string_view>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

class SerdRdfReader {
public:
    std::vector<RDFTriple> read(std::string_view turtle) const;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SERD_RDF_READER_H
