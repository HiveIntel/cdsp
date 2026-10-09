#include "rdf_mapper.h"

#include <stdexcept>

namespace rdf_mapper {

RDFMapper::RDFMapper(const std::vector<std::filesystem::path>& shape_paths, RDFFormat format)
    : format_(format) {
    (void) shape_paths;
    throw std::logic_error("RDFMapper shape-path constructor is not implemented");
}

RDFMapper::RDFMapper(const std::filesystem::path& shape_directory, RDFFormat format)
    : format_(format) {
    (void) shape_directory;
    throw std::logic_error("RDFMapper shape-directory constructor is not implemented");
}

void RDFMapper::loadSignalShapes(const std::vector<std::filesystem::path>& shape_paths) {
    (void) shape_paths;
    throw std::logic_error("RDFMapper::loadSignalShapes is not implemented");
}

std::vector<RDFTriple> RDFMapper::signalToTriples(const il_message::DataMessageDTO& message) const {
    (void) message;
    throw std::logic_error("RDFMapper::signalToTriples is not implemented");
}

std::string RDFMapper::signalToRDF(const il_message::DataMessageDTO& message) const {
    (void) message;
    throw std::logic_error("RDFMapper::signalToRDF is not implemented");
}

}  // namespace rdf_mapper
