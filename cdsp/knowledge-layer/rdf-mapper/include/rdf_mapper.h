#ifndef RDF_MAPPER_RDF_MAPPER_H
#define RDF_MAPPER_RDF_MAPPER_H

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "il_message_dto.h"
#include "rdf_model.h"
#include "serd_rdf_writer.h"
#include "shacl_parser.h"

namespace rdf_mapper {

/**
 * @brief Instantiate vehicle structure and signal observations from an indexed shape graph.
 *
 * This mapper applies the package's SHACL mapping conventions, not full SHACL validation.
 * Construction owns the parsed shapes; subsequent mapping/serialization calls are const
 * and keep all per-message traversal and output state local to the call.
 */
class RDFMapper {
   public:
    /**
     * @brief Load shapes and compile signal lookups and vehicle containment indexes.
     *
     * Both Turtle files are parsed together so signal-side sh:node references can
     * resolve vehicle shapes. Prefix/base declarations must be compatible, and
     * explicit blank-node labels must not collide between the files.
     * A nonempty vss:path identifies a signal shape; shapes without vss:path form
     * one vehicle tree. Signal-side sh:node bindings are not containment edges.
     * A child's target-class local name must be <parent-local-name>_<part-name>;
     * the actual tree, not underscores alone, determines its instance path.
     * No triples or serialized RDF are emitted by this constructor.
     *
     * @param vehicle_shapes_file Turtle file describing vehicle structure.
     * @param signal_shapes_file Turtle file describing signal observations.
     * @param instance_base_iri ASCII HTTP(S) instance namespace ending in '/'.
     *        Supply an unbracketed host and optional decimal port, without userinfo,
     *        query, fragment or literal dot segments. IP-literal hosts are not
     *        supported. This is independent of the files' vocabulary namespaces.
     * @throws std::invalid_argument If the instance namespace is unsuitable.
     * @throws std::runtime_error If an input cannot be read/parsed, a signal property
     *         binding/datatype is unsupported or ambiguous, or its shape
     *         topology, signal keys or generated instance paths are ambiguous.
     */
    RDFMapper(const std::filesystem::path& vehicle_shapes_file,
              const std::filesystem::path& signal_shapes_file, std::string instance_base_iri);

    /**
     * @brief Instantiate the complete vehicle tree without mapping literal properties.
     *
     * Emit each entity's target class as rdf:type and each containment edge in its
     * forward direction. The vehicle ID is caller-owned, not inferred from an IL instance.
     * @throws std::invalid_argument If vehicle_id is empty.
     */
    std::vector<RDFTriple> vehicleToTriples(const std::string& vehicle_id) const;

    /**
     * @brief Map signal observations and only their referenced vehicle branches.
     *
     * Data and metadata keys must match vss:path exactly, including case and prefix.
     * Each data path requires its own generated timestamp; root metadata, received
     * time and the current clock are not fallbacks. vss:value supports "value" and
     * "timestamp"; sh:hasValue supplies constants, and sh:node binds vehicle instances.
     * Dynamic values require sh:datatype (string, boolean, integer, float or double);
     * timestamps require dateTime. Numeric values may be rounded to the declared
     * float/double precision. Shared entities and ancestor links are emitted once.
     * An empty data vector produces no triples. No output is returned on failure.
     *
     * @param vehicle_id Explicit vehicle identity, independent of message.instance.
     * @throws std::invalid_argument If the ID, signal key, value or generated metadata
     *         is invalid; duplicate data/metadata paths are ambiguous and rejected.
     * @throws std::runtime_error If a timestamp cannot be formatted as UTC; shape bindings
     *         and datatypes are validated by the constructor.
     */
    std::vector<RDFTriple> signalToTriples(const il_message::DataMessageDTO& message,
                                           const std::string& vehicle_id) const;

    /// Map and serialize one signal batch; shares signalToTriples' exact-key/identity contract.
    /// format selects the output syntax, not the shape input syntax (always Turtle).
    std::string signalToRdf(const il_message::DataMessageDTO& message,
                            const std::string& vehicle_id,
                            RDFFormat format = RDFFormat::Turtle) const;

    /// Serialize the structural vehicle graph; no literal vehicle properties are emitted.
    /// Supports the Serd writer's four formats; quad syntaxes use the default graph.
    std::string vehicleToRdf(const std::string& vehicle_id,
                             RDFFormat format = RDFFormat::Turtle) const;

   private:
    struct VehicleParent {
        /// Owning parent entity; all parsed entities also remain in node_shapes_.
        NodeShapePtr shape;
        /// Forward containment predicate, even when this index is traversed backward.
        RDFNode predicate;
    };

    /// Compile exact signal keys, a single-parent vehicle tree and relative IRI paths.
    void buildShapeIndexes();

    /**
     * @brief Create a stable IRI for a vehicle or one of its indexed parts.
     *
     * The root is <base>Vehicle_<encoded-id>; parts append their containment path.
     * @throws std::invalid_argument If the ID is empty or the shape is not a vehicle entity.
     */
    RDFNode vehicleInstanceIri(std::string_view vehicle_id, const NodeShape& shape) const;

    /**
     * @brief Identify a signal instance by vehicle, signal path and generated time.
     *
     * Remove one leading "Vehicle." from the signal key and encode each dot-separated
     * component under the lowercased local name of the shape's target class (for
     * example /observation/ for sosa:Observation). Keep lookup keys unchanged and case-sensitive.
     * The caller supplies the root vehicle IRI and the UTC generated-time text, so both
     * are computed once and reused; identical inputs yield the same IRI, including on replay.
     * @throws std::invalid_argument If the shape is not an indexed signal shape.
     */
    RDFNode signalInstanceIri(const RDFNode& vehicle_root, const NodeShape& shape,
                              std::string_view generated_time) const;

    /// Append the missing root-to-entity branch, retaining forward containment predicates.
    /// emitted_entities belongs to one mapping call, not persistent mapper state.
    void appendVehicleBranchTriples(std::string_view vehicle_id, const NodeShape& shape,
                                    std::vector<RDFTriple>& triples,
                                    std::unordered_set<const NodeShape*>& emitted_entities) const;

    /// Caller-supplied namespace shared by structural and observation instances.
    std::string instance_base_iri_;

    /// Owns every parsed shape so PropertyShape::sh_node weak references stay valid.
    std::vector<NodeShapePtr> node_shapes_;

    /// Exact custom vss:path keys; no case folding or schema-prefix normalization.
    std::unordered_map<std::string, NodeShapePtr> signal_shapes_by_path_;

    /// Unique entity with no incoming vehicle containment edge; never an observation.
    NodeShapePtr vehicle_root_shape_;

    /// Child -> parent and forward RDF predicate, for later part-to-root traversal.
    std::unordered_map<const NodeShape*, VehicleParent> vehicle_parents_;

    /// Encoded relative paths; empty for root, signals start with their lowercased class name.
    /// Pointer keys are identity lookups only and never contribute to generated IRIs.
    std::unordered_map<const NodeShape*, std::string> instance_paths_by_shape_;
};

}  // namespace rdf_mapper

#endif  // RDF_MAPPER_RDF_MAPPER_H
