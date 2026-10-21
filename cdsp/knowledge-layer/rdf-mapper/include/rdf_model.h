#ifndef RDF_MAPPER_RDF_MODEL_H
#define RDF_MAPPER_RDF_MODEL_H

/**
 * @file rdf_model.h
 * @brief Owned RDF terms, triples, and graph queries used by the mapping pipeline.
 */

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace rdf_mapper {

/// Requested RDF syntax; each reader/writer defines which formats it implements.
enum class RDFFormat {
    Turtle,    ///< Compact RDF graph syntax with prefix/base declarations.
    NTriples,  ///< Line-oriented triple syntax with expanded IRIs.
    NQuads,    ///< Line-oriented quad syntax; RDFTriple itself carries no graph name.
    TriG       ///< Dataset syntax; RDFTriple itself carries no graph name.
};

/// Stored term category, without lexical validation or prefix expansion.
enum class RDFNodeType {
    Curie,      ///< Prefixed name retained as supplied, not an expanded IRI.
    Iri,        ///< IRI text retained as supplied; absolute form is not enforced here.
    BlankNode,  ///< Identifier local to the RDF document/graph that supplied it.
    Literal     ///< Lexical value with an optional datatype IRI; language tags are absent.
};

/**
 * @brief An RDF term whose lexical strings are owned by this value.
 *
 * Factories copy their inputs without validating RDF syntax. Equality compares
 * the stored representation, including datatype presence; it does not normalize
 * prefixes, IRIs, or equivalent literal values. Language-tagged literals cannot
 * be represented by this model.
 */
struct RDFNode {
    /// Stored term category; a default node is an empty CURIE.
    RDFNodeType type = RDFNodeType::Curie;
    /// IRI, prefixed name, blank-node identifier, or literal lexical value.
    std::string value;
    /// Optional literal datatype text; factories leave it absent for resource nodes.
    std::optional<std::string> datatype;

    /// Owns a prefixed name without resolving its prefix.
    static RDFNode curie(const std::string& value);
    /// Owns an IRI without checking whether it is absolute or well formed.
    static RDFNode iri(const std::string& value);
    /// Owns the blank-node identifier verbatim; document scoping is caller-owned.
    static RDFNode blankNode(const std::string& identifier);
    /// Owns a lexical value and optional datatype without validating their compatibility.
    static RDFNode literal(const std::string& value,
                           std::optional<std::string> datatype = std::nullopt);
};

/// Structural equality over type, value, and optional datatype.
bool operator==(const RDFNode& left, const RDFNode& right);
/// Negation of structural node equality.
bool operator!=(const RDFNode& left, const RDFNode& right);

/// Writes unescaped diagnostic text, not Turtle or another RDF serialization.
inline std::ostream& operator<<(std::ostream& out, const RDFNode& node) {
    const char* type = "Curie";
    switch (node.type) {
        case RDFNodeType::Curie:
            break;
        case RDFNodeType::Iri:
            type = "Iri";
            break;
        case RDFNodeType::BlankNode:
            type = "BlankNode";
            break;
        case RDFNodeType::Literal:
            type = "Literal";
            break;
    }

    out << "RDFNode{type=" << type << ", value=" << node.value;
    if (node.datatype.has_value()) {
        out << ", datatype=" << *node.datatype;
    }
    return out << '}';
}

/// Owned subject/predicate/object terms; construction does not validate RDF term positions.
struct RDFTriple {
    /// Subject term; readers/writers apply their own resource-node restrictions.
    RDFNode subject;
    /// Predicate term; graph queries below match expanded IRI predicates only.
    RDFNode predicate;
    /// Resource or literal object term.
    RDFNode object;
};

/// Structural equality over all three terms.
bool operator==(const RDFTriple& left, const RDFTriple& right);
/// Negation of structural triple equality.
bool operator!=(const RDFTriple& left, const RDFTriple& right);

/// Writes diagnostic node representations, not a legal RDF statement.
inline std::ostream& operator<<(std::ostream& out, const RDFTriple& triple) {
    return out << "RDFTriple{subject=" << triple.subject << ", predicate=" << triple.predicate
               << ", object=" << triple.object << '}';
}

/**
 * @brief Owns an ordered triple collection and provides exact-match graph queries.
 *
 * Storage retains duplicate triples. Query results suppress structurally equal
 * nodes while preserving their first matching occurrence in the stored order.
 * Queries scan the collection, return owned copies, and do not resolve CURIEs or
 * perform RDF entailment, datatype canonicalization, or SHACL validation.
 */
class RDFGraph {
   public:
    /// Takes ownership of the supplied triples without sorting or deduplicating them.
    explicit RDFGraph(std::vector<RDFTriple> triples);

    /// Returns distinct objects for an exact subject and an IRI-valued predicate.
    std::vector<RDFNode> objects(const RDFNode& subject, std::string_view predicate_iri) const;
    /**
     * @brief Returns the sole distinct object, or nullopt when no triple matches.
     * @throws std::runtime_error If more than one structurally distinct object matches.
     * @note Repeated identical triples do not violate this singleton requirement.
     */
    std::optional<RDFNode> object(const RDFNode& subject, std::string_view predicate_iri) const;
    /// Returns distinct subjects for an IRI-valued predicate and an exact object.
    std::vector<RDFNode> subjects(std::string_view predicate_iri, const RDFNode& object) const;

   private:
    /// Original ordered input, including duplicates; queries do not mutate it.
    std::vector<RDFTriple> triples_;

    /// Writes every stored triple as diagnostic text, including duplicate statements.
    friend std::ostream& operator<<(std::ostream& out, const RDFGraph& graph) {
        out << "RDFGraph{triples=[";
        for (std::size_t index = 0; index < graph.triples_.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << graph.triples_[index];
        }
        return out << "]}";
    }
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_MODEL_H
