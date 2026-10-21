#ifndef RDF_MAPPER_SERD_RDF_WRITER_H
#define RDF_MAPPER_SERD_RDF_WRITER_H

#include <map>
#include <string>
#include <vector>

#include "rdf_model.h"

namespace rdf_mapper {

/**
 * @brief Serialize an owned triple collection using the Serd C writer.
 *
 * Prefixes configure both CURIE expansion and Turtle/TriG namespace directives.
 * Every write owns a separate Serd environment, writer and output buffer; const
 * writes may run concurrently provided no thread calls setPrefix on this object.
 * RDFTriple has no graph name or literal language tag, so neither is invented.
 */
class SerdRdfWriter {
   public:
    /// Validate and retain prefixes; an empty name denotes the default prefix.
    explicit SerdRdfWriter(std::map<std::string, std::string> prefixes = {});

    /**
     * @brief Add or replace an absolute namespace IRI without emitting output.
     *
     * Nonempty names use the portable ASCII subset [A-Za-z][A-Za-z0-9_.-]*,
     * without a trailing dot or ':'. Names and IRIs must not contain NUL bytes.
     * @throws std::invalid_argument If the prefix name or IRI is invalid.
     */
    void setPrefix(const std::string& prefix, const std::string& iri);

    /**
     * @brief Return a complete RDF document; never return partial output on failure.
     *
     * Supports Turtle, N-Triples, N-Quads and TriG. Quad syntaxes contain only the
     * default graph; full Notation3 is not supported by this Serd API.
     * Subjects are IRIs/CURIEs/blank nodes, predicates are IRIs/CURIEs, and objects
     * may additionally be literals. CURIE nodes require a registered prefix and
     * expand to absolute IRIs. A literal datatype string expands if its prefix is
     * registered, otherwise it is an absolute IRI (including custom URI schemes).
     *
     * Preserve literal lexical text, even when it is not valid for its datatype;
     * this is an RDF serializer, not an XSD/SHACL validator. Serd handles escaping.
     * Input strings must be well-formed UTF-8 without embedded NULs. Blank labels
     * use [A-Za-z0-9_][A-Za-z0-9_.-]* without a trailing dot or '_:' marker.
     * Blank labels are scoped to the returned document, not globally identified.
     * Supply RFC 3987-conforming IRIs: checks cover absolute spelling, characters
     * and percent escapes, not complete authority or scheme-specific validation.
     *
     * @throws std::invalid_argument If format, a node or its text is unsupported/invalid.
     * @throws std::runtime_error If Serd cannot finish the document.
     * Output-callback allocation failures are rethrown after returning to C++.
     */
    std::string write(const std::vector<RDFTriple>& triples,
                      RDFFormat format = RDFFormat::Turtle) const;

   private:
    /// Validated namespace bindings; no Serd handles are retained between writes.
    std::map<std::string, std::string> prefixes_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_SERD_RDF_WRITER_H
