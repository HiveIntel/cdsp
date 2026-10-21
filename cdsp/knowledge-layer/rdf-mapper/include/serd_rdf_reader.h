#ifndef RDF_MAPPER_SERD_RDF_READER_H
#define RDF_MAPPER_SERD_RDF_READER_H

/**
 * @file serd_rdf_reader.h
 * @brief Synchronous Turtle-to-triple adapter with owned C++ results.
 */

#include <string_view>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

/**
 * @brief Reads the Turtle subset representable by RDFNode and RDFTriple.
 *
 * Every call owns an independent Serd environment, reader, and callback context.
 * Prefix/base directives are applied while reading; no external base IRI is supplied.
 * Results own their strings and retain statement order and duplicates. Blank-node
 * labels remain local to this parse; callers combining documents must manage scope.
 * Language-tagged literals and named graphs are rejected. Reading does not perform
 * RDF entailment, datatype lexical validation, or SHACL validation.
 */
class SerdRdfReader {
   public:
    /**
     * @brief Parses borrowed Turtle text synchronously and returns owned triples.
     * @param turtle UTF-8 input; embedded null bytes are rejected before Serd reads it.
     * @return Statements in Serd callback order, with CURIEs expanded and datatype
     *         IRIs resolved against the active environment. No absolute-IRI check is added.
     * @throws std::runtime_error On resource setup failure, a recorded callback
     *         failure, or an unsuccessful Serd parse status. Partial triples are not returned.
     * @note C callbacks are noexcept and diagnostics are best effort. Recorded
     *       adapter failures are reported after Serd returns. Ordinary C++ setup/result
     *       allocation exceptions can propagate from this method.
     */
    std::vector<RDFTriple> read(std::string_view turtle) const;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SERD_RDF_READER_H
