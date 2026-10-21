#include "rdf_mapper.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>

namespace rdf_mapper {
namespace {

/// Encode raw bytes as one URI path component; an input '/' must remain data.
std::string encodeIriSegment(std::string_view segment) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char character : segment) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '.' ||
            character == '_' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[character >> 4]);
            encoded.push_back(hex[character & 0x0F]);
        }
    }
    return encoded;
}

/// VSS naming convention supplies labels; targetClass still defines emitted rdf:type.
std::string_view classLocalName(const NodeShape& shape) {
    const std::string_view iri = shape.sh_target_class.value;
    const auto separator = iri.find_last_of("#/");
    const auto local_name = separator == std::string_view::npos ? iri : iri.substr(separator + 1);
    if (local_name.empty()) {
        throw std::runtime_error("Target class has no local name: " + std::string(iri));
    }
    return local_name;
}

/// Preserve subsecond precision and avoid overflowing at system_clock::time_point::min().
std::string generatedTimeText(std::chrono::system_clock::time_point time) {
    const auto elapsed = time.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    const auto remainder =
        elapsed - std::chrono::duration_cast<std::chrono::system_clock::duration>(seconds);
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(remainder).count();
    if (nanos < 0) {
        --seconds;
        nanos += 1000000000;
    }

    const auto seconds_count = seconds.count();
    if (static_cast<long double>(seconds_count) <
            static_cast<long double>(std::numeric_limits<std::time_t>::min()) ||
        static_cast<long double>(seconds_count) >
            static_cast<long double>(std::numeric_limits<std::time_t>::max())) {
        throw std::runtime_error("Generated timestamp is outside the UTC conversion range");
    }

    const std::time_t utc_seconds = static_cast<std::time_t>(seconds_count);
    std::tm utc{};
#ifdef _WIN32
    if (::gmtime_s(&utc, &utc_seconds) != 0) {
#else
    if (::gmtime_r(&utc_seconds, &utc) == nullptr) {
#endif
        throw std::runtime_error("Unable to convert generated timestamp to UTC");
    }

    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(9)
         << nanos << 'Z';
    if (!text) {
        throw std::runtime_error("Unable to format generated timestamp");
    }
    return text.str();
}

/// Value range of xsd:integer and its derived types; the DTO carries at most int64.
std::optional<std::pair<std::int64_t, std::int64_t>> integerRange(std::string_view local_type) {
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if (local_type == "integer" || local_type == "long") return {{min, max}};
    if (local_type == "int") return {{INT32_MIN, INT32_MAX}};
    if (local_type == "short") return {{INT16_MIN, INT16_MAX}};
    if (local_type == "byte") return {{INT8_MIN, INT8_MAX}};
    if (local_type == "unsignedLong" || local_type == "nonNegativeInteger") return {{0, max}};
    if (local_type == "unsignedInt") return {{0, UINT32_MAX}};
    if (local_type == "unsignedShort") return {{0, UINT16_MAX}};
    if (local_type == "unsignedByte") return {{0, UINT8_MAX}};
    if (local_type == "positiveInteger") return {{1, max}};
    if (local_type == "nonPositiveInteger") return {{min, 0}};
    if (local_type == "negativeInteger") return {{min, -1}};
    return std::nullopt;
}

/// Datatypes signalValueLiteral can produce; checked once when shapes are indexed.
bool isSupportedValueDatatype(std::string_view type) {
    constexpr std::string_view xsd = "http://www.w3.org/2001/XMLSchema#";
    if (type.compare(0, xsd.size(), xsd) != 0) {
        return false;
    }
    const auto local_type = type.substr(xsd.size());
    return local_type == "string" || local_type == "boolean" || local_type == "float" ||
           local_type == "double" || integerRange(local_type).has_value();
}

/// Convert DTO scalars to the declared XSD value space; do not relabel arbitrary text.
RDFNode signalValueLiteral(const il_data_point::DataPointDTO& point, const RDFNode& datatype) {
    constexpr std::string_view xsd = "http://www.w3.org/2001/XMLSchema#";
    const std::string_view type = datatype.value;
    if (type.compare(0, xsd.size(), xsd) != 0) {
        throw std::runtime_error("Unsupported signal value datatype: " + datatype.value);
    }
    const auto local_type = type.substr(xsd.size());
    if (local_type == "string") {
        if (const auto* value = std::get_if<std::string>(&point.value)) {
            return RDFNode::literal(*value, datatype.value);
        }
    } else if (local_type == "boolean") {
        if (const auto* value = std::get_if<bool>(&point.value)) {
            return RDFNode::literal(*value ? "true" : "false", datatype.value);
        }
    } else if (const auto range = integerRange(local_type)) {
        if (const auto* value = std::get_if<std::int64_t>(&point.value)) {
            if (*value < range->first || *value > range->second) {
                throw std::invalid_argument("Signal value exceeds xsd:" + std::string(local_type) +
                                            " range: " + point.path);
            }
            return RDFNode::literal(std::to_string(*value), datatype.value);
        }
    } else if (local_type == "float" || local_type == "double") {
        long double number;
        if (const auto* value = std::get_if<double>(&point.value)) {
            number = *value;
        } else if (const auto* value = std::get_if<std::int64_t>(&point.value)) {
            number = *value;
        } else {
            throw std::invalid_argument("Signal requires a numeric value: " + point.path);
        }
        if (!std::isfinite(number)) {
            throw std::invalid_argument("Signal requires a finite numeric value: " + point.path);
        }

        std::ostringstream lexical;
        lexical.imbue(std::locale::classic());
        if (local_type == "float") {
            if (std::abs(number) > std::numeric_limits<float>::max()) {
                throw std::invalid_argument("Signal value exceeds xsd:float range: " + point.path);
            }
            const float value = static_cast<float>(number);
            if (number != 0 && value == 0) {
                throw std::invalid_argument("Signal value underflows xsd:float: " + point.path);
            }
            lexical << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
        } else {
            lexical << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << static_cast<double>(number);
        }
        if (!lexical) {
            throw std::runtime_error("Unable to format signal value: " + point.path);
        }
        return RDFNode::literal(lexical.str(), datatype.value);
    } else {
        throw std::runtime_error("Unsupported signal value datatype: " + datatype.value);
    }
    throw std::invalid_argument("Signal value does not match its declared datatype: " + point.path);
}

}  // namespace

RDFMapper::RDFMapper(const std::filesystem::path& vehicle_shapes_file,
                     const std::filesystem::path& signal_shapes_file, std::string instance_base_iri)
    : instance_base_iri_(std::move(instance_base_iri)) {
    const std::string_view base = instance_base_iri_;
    const std::size_t authority_start = base.compare(0, 8, "https://") == 0 ? 8 : 7;
    const auto path_start = base.find('/', authority_start);
    if ((base.compare(0, 8, "https://") != 0 && base.compare(0, 7, "http://") != 0) ||
        path_start == std::string_view::npos || path_start == authority_start ||
        base.back() != '/' || base.find_first_of("?#") != std::string_view::npos) {
        throw std::invalid_argument(
            "Instance namespace must be an absolute HTTP(S) IRI ending in '/', without query or "
            "fragment");
    }

    // A namespace prefix is not a connection endpoint; support a small explicit
    // authority contract instead of silently accepting arbitrary malformed URLs.
    const auto authority = base.substr(authority_start, path_start - authority_start);
    const auto port_separator = authority.find(':');
    const auto host = authority.substr(0, port_separator);
    if (host.empty() ||
        host.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                               "-._~!$&'()*+,;=%") != std::string_view::npos) {
        throw std::invalid_argument(
            "Instance namespace requires an unbracketed host without userinfo");
    }
    if (port_separator != std::string_view::npos) {
        const auto port = authority.substr(port_separator + 1);
        if (port.empty() || port.find_first_not_of("0123456789") != std::string_view::npos) {
            throw std::invalid_argument("Instance namespace port must contain decimal digits");
        }
    }
    for (std::size_t index = 0; index < base.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(base[index]);
        if (character <= 0x20 || character >= 0x7F ||
            std::string_view("<>\"\\{}|^`").find(static_cast<char>(character)) !=
                std::string_view::npos) {
            throw std::invalid_argument("Instance namespace contains an unencoded character");
        }
        if (index >= path_start &&
            std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                             "-._~!$&'()*+,;=:@/%")
                    .find(static_cast<char>(character)) == std::string_view::npos) {
            throw std::invalid_argument("Instance namespace contains an invalid path character");
        }
        if (character == '%') {
            if (index + 2 >= base.size() ||
                base.substr(index + 1, 2).find_first_not_of("0123456789abcdefABCDEF") !=
                    std::string_view::npos) {
                throw std::invalid_argument(
                    "Instance namespace contains an invalid percent escape");
            }
            index += 2;
        }
    }
    for (std::size_t start = path_start + 1; start < base.size();) {
        const auto end =
            base.find('/', start);  // The required trailing '/' terminates each segment.
        const auto segment = base.substr(start, end - start);
        if (segment == "." || segment == "..") {
            throw std::invalid_argument("Instance namespace must not contain dot segments");
        }
        start = end + 1;
    }

    std::string turtle;
    for (const std::filesystem::path& shape_file : {vehicle_shapes_file, signal_shapes_file}) {
        if (!std::filesystem::is_regular_file(shape_file)) {
            throw std::runtime_error("SHACL input is not a regular file: " + shape_file.string());
        }

        std::ifstream input(shape_file, std::ios::binary);
        if (!input.is_open()) {
            throw std::runtime_error("Unable to open SHACL file: " + shape_file.string());
        }

        char buffer[4096];
        while (input.read(buffer, sizeof(buffer))) {
            turtle.append(buffer, sizeof(buffer));
        }
        if (input.bad() || !input.eof()) {
            throw std::runtime_error("Unable to read SHACL file: " + shape_file.string());
        }
        turtle.append(buffer, static_cast<std::size_t>(input.gcount()));

        // A final line comment must not consume the next file's first statement.
        turtle.push_back('\n');
    }

    // Parse once: separate parses cannot resolve cross-file sh:node references.
    node_shapes_ = SHACLParser{}.parse(turtle);
    buildShapeIndexes();
}

void RDFMapper::buildShapeIndexes() {
    std::vector<NodeShapePtr> vehicle_shapes;
    std::unordered_set<std::string> instance_paths;
    // Encoded root segments that signal instances occupy; vehicle parts must avoid them.
    std::unordered_set<std::string> signal_class_segments;
    for (const NodeShapePtr& shape : node_shapes_) {
        if (!shape->vss_path.has_value()) {
            vehicle_shapes.push_back(shape);
            continue;
        }

        const std::string& key = shape->vss_path->value;
        if (key.empty() || !signal_shapes_by_path_.emplace(key, shape).second) {
            throw std::runtime_error("Signal vss:path is empty or duplicated: " + key);
        }

        // Normalize only the generated path, not the signal lookup key.
        std::string_view path = key;
        if (path.compare(0, 8, "Vehicle.") == 0) {
            path.remove_prefix(8);
        }
        // The signal's own target class names its root segment (sosa:Observation ->
        // "observation"), so instance IRIs follow the ontology rather than a fixed word.
        std::string class_segment(classLocalName(*shape));
        for (char& character : class_segment) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        if (class_segment == "." || class_segment == "..") {
            throw std::runtime_error("Signal target class has a reserved IRI component: " +
                                     shape->sh_target_class.value);
        }
        std::string relative_path = encodeIriSegment(class_segment);
        signal_class_segments.insert(relative_path);
        std::size_t start = 0;
        do {
            const auto dot = path.find('.', start);
            const auto end = dot == std::string_view::npos ? path.size() : dot;
            const auto segment = path.substr(start, end - start);
            if (segment.empty()) {
                throw std::runtime_error("Signal vss:path contains an empty component: " + key);
            }
            relative_path += '/' + encodeIriSegment(segment);
            if (dot == std::string_view::npos) {
                break;
            }
            start = dot + 1;
        } while (start <= path.size());

        if (!instance_paths.insert(relative_path).second) {
            throw std::runtime_error("Signal keys produce the same signal IRI path: " + key);
        }
        instance_paths_by_shape_.emplace(shape.get(), std::move(relative_path));
    }
    if (vehicle_shapes.empty() || signal_shapes_by_path_.empty()) {
        throw std::runtime_error("Mapping requires vehicle shapes and signal shapes with vss:path");
    }

    // Only vehicle -> vehicle references describe containment; FOI links stay separate.
    for (const NodeShapePtr& parent : vehicle_shapes) {
        for (const PropertyShape& property : parent->sh_properties) {
            const NodeShapePtr child = property.sh_node.lock();
            if (!child) {
                continue;  // Literal and constant properties are not structural edges.
            }
            if (child->vss_path.has_value()) {
                throw std::runtime_error("Vehicle containment references a signal shape");
            }
            if (!vehicle_parents_.emplace(child.get(), VehicleParent{parent, property.sh_path})
                     .second) {
                throw std::runtime_error("Vehicle part has more than one containment route: " +
                                         child->sh_target_class.value);
            }
        }
    }
    for (const NodeShapePtr& shape : vehicle_shapes) {
        if (vehicle_parents_.find(shape.get()) == vehicle_parents_.end()) {
            if (vehicle_root_shape_) {
                throw std::runtime_error("Vehicle topology must have exactly one root");
            }
            vehicle_root_shape_ = shape;
        }
    }
    if (!vehicle_root_shape_) {
        throw std::runtime_error("Vehicle topology has no root; containment contains a cycle");
    }

    // Iterative root-first traversal avoids recursion on deep vehicle trees.
    std::vector<NodeShapePtr> reachable{vehicle_root_shape_};
    instance_paths_by_shape_.emplace(vehicle_root_shape_.get(), "");
    for (std::size_t index = 0; index < reachable.size(); ++index) {
        const NodeShapePtr parent = reachable[index];
        const std::string parent_path = instance_paths_by_shape_.at(parent.get());
        const std::string class_prefix = std::string(classLocalName(*parent)) + '_';
        for (const PropertyShape& property : parent->sh_properties) {
            const NodeShapePtr child = property.sh_node.lock();
            if (!child) {
                continue;
            }
            const std::string_view child_class = classLocalName(*child);
            if (child_class.compare(0, class_prefix.size(), class_prefix) != 0) {
                throw std::runtime_error("Vehicle child class must extend its parent class name: " +
                                         child->sh_target_class.value);
            }
            const auto segment = child_class.substr(class_prefix.size());
            if (segment.empty() || segment == "." || segment == ".." ||
                (parent_path.empty() && signal_class_segments.count(encodeIriSegment(segment)))) {
                throw std::runtime_error("Vehicle part has an empty or reserved IRI component: " +
                                         child->sh_target_class.value);
            }
            const std::string child_path =
                (parent_path.empty() ? "" : parent_path + '/') + encodeIriSegment(segment);
            if (!instance_paths.insert(child_path).second) {
                throw std::runtime_error("Vehicle parts produce the same instance IRI path: " +
                                         child_path);
            }
            instance_paths_by_shape_.emplace(child.get(), child_path);
            reachable.push_back(child);
        }
    }
    if (reachable.size() != vehicle_shapes.size()) {
        throw std::runtime_error(
            "Vehicle topology contains a cycle or a part unreachable from its root");
    }

    // Validate every signal binding once here so mapping never meets a malformed shape.
    for (const auto& entry : signal_shapes_by_path_) {
        for (const PropertyShape& property : entry.second->sh_properties) {
            const NodeShapePtr target = property.sh_node.lock();
            const unsigned binding_count =
                static_cast<unsigned>(static_cast<bool>(target)) +
                static_cast<unsigned>(property.sh_has_value.has_value()) +
                static_cast<unsigned>(property.vss_value.has_value());
            if (binding_count != 1) {
                throw std::runtime_error("Signal property must have exactly one value binding: " +
                                         property.sh_path.value);
            }

            if (target) {
                if (target->vss_path.has_value() ||
                    instance_paths_by_shape_.find(target.get()) == instance_paths_by_shape_.end()) {
                    throw std::runtime_error(
                        "Signal sh:node must bind a reachable vehicle shape: " + entry.first);
                }
                if (property.sh_datatype.has_value()) {
                    throw std::runtime_error("Vehicle instance binding cannot have sh:datatype: " +
                                             property.sh_path.value);
                }
            } else if (property.sh_has_value.has_value()) {
                const RDFNode& constant = *property.sh_has_value;
                if (constant.type == RDFNodeType::BlankNode) {
                    throw std::runtime_error(
                        "Blank-node constants are not supported by signal mapping: " +
                        property.sh_path.value);
                }
                if (property.sh_datatype.has_value() &&
                    (constant.type != RDFNodeType::Literal ||
                     constant.datatype.value_or("http://www.w3.org/2001/XMLSchema#string") !=
                         property.sh_datatype->value)) {
                    throw std::runtime_error("Signal constant conflicts with sh:datatype: " +
                                             property.sh_path.value);
                }
            } else {
                if (!property.sh_datatype.has_value()) {
                    throw std::runtime_error("Dynamic signal property requires sh:datatype: " +
                                             property.sh_path.value);
                }
                if (*property.vss_value == "value") {
                    if (!isSupportedValueDatatype(property.sh_datatype->value)) {
                        throw std::runtime_error("Unsupported signal value datatype: " +
                                                 property.sh_datatype->value);
                    }
                } else if (*property.vss_value == "timestamp") {
                    if (property.sh_datatype->value !=
                        "http://www.w3.org/2001/XMLSchema#dateTime") {
                        throw std::runtime_error("Signal timestamp requires xsd:dateTime: " +
                                                 property.sh_path.value);
                    }
                } else {
                    throw std::runtime_error("Unsupported vss:value selector: " +
                                             *property.vss_value);
                }
            }
        }
    }
}

RDFNode RDFMapper::vehicleInstanceIri(std::string_view vehicle_id, const NodeShape& shape) const {
    const auto path = instance_paths_by_shape_.find(&shape);
    if (vehicle_id.empty() || shape.vss_path.has_value() ||
        path == instance_paths_by_shape_.end()) {
        throw std::invalid_argument(
            "Vehicle IRI requires a nonempty ID and an indexed vehicle shape");
    }
    std::string iri = instance_base_iri_ + "Vehicle_" + encodeIriSegment(vehicle_id);
    if (!path->second.empty()) {
        iri += '/' + path->second;
    }
    return RDFNode::iri(iri);
}

RDFNode RDFMapper::signalInstanceIri(const RDFNode& vehicle_root, const NodeShape& shape,
                                     std::string_view generated_time) const {
    const auto path = instance_paths_by_shape_.find(&shape);
    if (!shape.vss_path.has_value() || path == instance_paths_by_shape_.end()) {
        throw std::invalid_argument("Signal IRI requires an indexed signal shape");
    }
    return RDFNode::iri(vehicle_root.value + '/' + path->second + '/' +
                        std::string(generated_time));
}

void RDFMapper::appendVehicleBranchTriples(
    std::string_view vehicle_id, const NodeShape& shape, std::vector<RDFTriple>& triples,
    std::unordered_set<const NodeShape*>& emitted_entities) const {
    std::vector<const NodeShape*> branch;
    const NodeShape* entity = &shape;
    while (entity && emitted_entities.find(entity) == emitted_entities.end()) {
        branch.push_back(entity);
        const auto parent = vehicle_parents_.find(entity);
        entity = parent == vehicle_parents_.end() ? nullptr : parent->second.shape.get();
    }

    // Walk backward internally, but emit root-first links in the original RDF direction.
    for (auto current = branch.rbegin(); current != branch.rend(); ++current) {
        const RDFNode subject = vehicleInstanceIri(vehicle_id, **current);
        const auto parent = vehicle_parents_.find(*current);
        if (parent != vehicle_parents_.end()) {
            triples.push_back({vehicleInstanceIri(vehicle_id, *parent->second.shape),
                               parent->second.predicate, subject});
        }
        triples.push_back(
            {subject, RDFNode::iri(std::string(rdf::type)), (*current)->sh_target_class});
        emitted_entities.insert(*current);
    }
}

std::vector<RDFTriple> RDFMapper::vehicleToTriples(const std::string& vehicle_id) const {
    // Validate even if traversal policy changes later; an empty ID is never an instance.
    (void) vehicleInstanceIri(vehicle_id, *vehicle_root_shape_);
    std::vector<RDFTriple> triples;
    std::unordered_set<const NodeShape*> emitted_entities;
    for (const NodeShapePtr& shape : node_shapes_) {
        if (!shape->vss_path.has_value()) {
            appendVehicleBranchTriples(vehicle_id, *shape, triples, emitted_entities);
        }
    }
    return triples;
}

std::vector<RDFTriple> RDFMapper::signalToTriples(const il_message::DataMessageDTO& message,
                                                  const std::string& vehicle_id) const {
    const RDFNode vehicle_root = vehicleInstanceIri(vehicle_id, *vehicle_root_shape_);
    // Metadata ordering is independent of data ordering. Duplicate keys are ambiguous,
    // even when their current values happen to match; no event-merge policy is implied.
    std::unordered_map<std::string, const il_data_point::TimestampsDTO*> timestamps_by_path;
    if (message.metadata.has_value()) {
        for (const auto& metadata : *message.metadata) {
            if (!timestamps_by_path.emplace(metadata.path, &metadata.timestamps).second) {
                throw std::invalid_argument("Duplicate signal metadata path: " + metadata.path);
            }
        }
    }
    if (message.data.empty()) {
        return {};
    }

    std::vector<RDFTriple> triples;
    std::unordered_set<std::string> data_paths;
    std::unordered_set<const NodeShape*> emitted_entities;
    for (const auto& point : message.data) {
        if (!data_paths.insert(point.path).second) {
            throw std::invalid_argument("Duplicate signal data path: " + point.path);
        }
        const auto indexed_shape = signal_shapes_by_path_.find(point.path);
        if (indexed_shape == signal_shapes_by_path_.end()) {
            throw std::invalid_argument("No signal shape for exact path: " + point.path);
        }
        const auto timestamps = timestamps_by_path.find(point.path);
        if (timestamps == timestamps_by_path.end() || !timestamps->second->generated.has_value()) {
            throw std::invalid_argument("Signal requires exact-path generated metadata: " +
                                        point.path);
        }

        const NodeShape& shape = *indexed_shape->second;
        const std::string generated_time = generatedTimeText(*timestamps->second->generated);
        const RDFNode subject = signalInstanceIri(vehicle_root, shape, generated_time);
        std::vector<RDFTriple> observation_triples;
        observation_triples.reserve(shape.sh_properties.size() + 1);
        observation_triples.push_back(
            {subject, RDFNode::iri(std::string(rdf::type)), shape.sh_target_class});
        for (const PropertyShape& property : shape.sh_properties) {
            // Bindings were validated by buildShapeIndexes; exactly one is present.
            RDFNode object;
            if (const NodeShapePtr target = property.sh_node.lock()) {
                object = vehicleInstanceIri(vehicle_id, *target);
                appendVehicleBranchTriples(vehicle_id, *target, triples, emitted_entities);
            } else if (property.sh_has_value.has_value()) {
                object = *property.sh_has_value;
            } else if (*property.vss_value == "value") {
                object = signalValueLiteral(point, *property.sh_datatype);
            } else {
                object = RDFNode::literal(generated_time, property.sh_datatype->value);
            }

            const RDFTriple triple{subject, property.sh_path, std::move(object)};
            // Only this observation can repeat these statements. Keep the scan local
            // instead of searching all previously mapped observations in the batch.
            if (std::find(observation_triples.begin(), observation_triples.end(), triple) ==
                observation_triples.end()) {
                observation_triples.push_back(triple);
            }
        }
        triples.insert(triples.end(), observation_triples.begin(), observation_triples.end());
    }
    return triples;
}

std::string RDFMapper::signalToRdf(const il_message::DataMessageDTO& message,
                                   const std::string& vehicle_id, RDFFormat format) const {
    return SerdRdfWriter{}.write(signalToTriples(message, vehicle_id), format);
}

std::string RDFMapper::vehicleToRdf(const std::string& vehicle_id, RDFFormat format) const {
    return SerdRdfWriter{}.write(vehicleToTriples(vehicle_id), format);
}

}  // namespace rdf_mapper
