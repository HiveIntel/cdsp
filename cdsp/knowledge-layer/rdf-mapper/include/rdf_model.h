#ifndef RDF_MAPPER_RDF_MODEL_H
#define RDF_MAPPER_RDF_MODEL_H

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace rdf_mapper {

enum class RDFFormat {
    Turtle,
    NTriples,
    NQuads,
    TriG
};

enum class RDFNodeType {
    Curie,
    Iri,
    BlankNode,
    Literal
};

struct RDFNode {
    RDFNodeType type = RDFNodeType::Curie;
    std::string value;
    std::optional<std::string> datatype;

    static RDFNode curie(const std::string& value);
    static RDFNode iri(const std::string& value);
    static RDFNode blankNode(const std::string& identifier);
    static RDFNode literal(const std::string& value,
                           std::optional<std::string> datatype = std::nullopt);
};

bool operator==(const RDFNode& left, const RDFNode& right);
bool operator!=(const RDFNode& left, const RDFNode& right);

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

struct RDFTriple {
    RDFNode subject;
    RDFNode predicate;
    RDFNode object;
};

bool operator==(const RDFTriple& left, const RDFTriple& right);
bool operator!=(const RDFTriple& left, const RDFTriple& right);

inline std::ostream& operator<<(std::ostream& out, const RDFTriple& triple) {
    return out << "RDFTriple{subject=" << triple.subject
               << ", predicate=" << triple.predicate
               << ", object=" << triple.object << '}';
}

class RDFGraph {
   public:
    explicit RDFGraph(std::vector<RDFTriple> triples);

    std::vector<RDFNode> objects(const RDFNode& subject,
                                 std::string_view predicate_iri) const;
    std::optional<RDFNode> object(const RDFNode& subject,
                                  std::string_view predicate_iri) const;
    std::vector<RDFNode> subjects(std::string_view predicate_iri,
                                  const RDFNode& object) const;

   private:
    std::vector<RDFTriple> triples_;

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
