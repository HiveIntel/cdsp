#include "rdf_writer.h"

#include <stdexcept>
#include <utility>

namespace rdf_mapper {

RDFWriter::RDFWriter(std::map<std::string, std::string> prefixes)
    : prefixes_(std::move(prefixes)) {}

void RDFWriter::setPrefix(const std::string& prefix, const std::string& uri) {
    if (uri.empty()) {
        throw std::runtime_error("RDF prefix URI cannot be empty");
    }

    prefixes_[prefix] = uri;
}

std::string RDFWriter::write(const std::vector<RDFTriple>& triples,
                             RDFFormat format) const {
    (void)triples;
    (void)format;
    return "";
}

}  // namespace rdf_mapper
