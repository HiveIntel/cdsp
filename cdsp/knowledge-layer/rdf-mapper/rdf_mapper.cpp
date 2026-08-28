#include "rdf_mapper.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "shacl_shape_parser.h"

namespace rdf_mapper {

namespace {

using json = nlohmann::json;

constexpr char kVehicleSchema[] = "Vehicle";

std::string jsonValueToLexical(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        return value.dump();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }

    return value.dump();
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string stripVehiclePrefix(const std::string& signal_path) {
    const std::string lower_signal_path = toLower(signal_path);
    if (startsWith(lower_signal_path, "vehicle.")) {
        return signal_path.substr(std::string("vehicle.").size());
    }
    return signal_path;
}

std::string signalNameForPath(const std::string& schema, const std::string& path) {
    const std::string normalized_schema = toLower(schema);
    const std::string normalized_path = toLower(path);
    const std::string schema_prefix = normalized_schema + ".";

    if (startsWith(normalized_path, schema_prefix)) {
        return normalized_path;
    }

    return schema_prefix + normalized_path;
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(path);
    while (std::getline(stream, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

void flattenData(const json& node,
                 const std::string& prefix,
                 std::vector<std::pair<std::string, std::string>>& output) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            std::string child_path;
            if (prefix.empty()) {
                child_path = it.key();
            } else if (it.key().empty()) {
                child_path = prefix;
            } else {
                child_path = prefix + "." + it.key();
            }
            flattenData(it.value(), child_path, output);
        }
        return;
    }

    if (!node.is_null() && !node.is_array()) {
        output.emplace_back(prefix, jsonValueToLexical(node));
    }
}

std::optional<std::string> findValueByPath(const json& data, const std::string& signal_path) {
    if (data.is_object() && data.contains("")) {
        return jsonValueToLexical(data.at(""));
    }

    const json* current = &data;
    for (const auto& part : splitPath(stripVehiclePrefix(signal_path))) {
        if (!current->is_object() || !current->contains(part)) {
            return std::nullopt;
        }
        current = &current->at(part);
    }

    if (current->is_null() || current->is_array() || current->is_object()) {
        return std::nullopt;
    }

    return jsonValueToLexical(*current);
}

std::string findSignalValue(const json& data, const std::string& signal_path) {
    if (auto value = findValueByPath(data, signal_path)) {
        return *value;
    }

    std::vector<std::pair<std::string, std::string>> flattened;
    flattenData(data, "", flattened);
    if (flattened.size() == 1) {
        return flattened.front().second;
    }

    throw std::runtime_error("Cannot resolve signal value for path: " + signal_path);
}

std::optional<json> metadataForPath(const json& metadata, const std::string& signal_path) {
    if (!metadata.is_object()) {
        return std::nullopt;
    }

    const std::string relative_path = stripVehiclePrefix(signal_path);
    for (const auto& key : {relative_path, signal_path, std::string("")}) {
        if (metadata.contains(key)) {
            return metadata.at(key);
        }
    }

    return std::nullopt;
}

std::optional<std::pair<long long, int>> readTimestamp(const json& metadata_node,
                                                       const std::string& timestamp_name) {
    if (!metadata_node.contains("timestamps")) {
        return std::nullopt;
    }

    const json& timestamps = metadata_node.at("timestamps");
    if (!timestamps.contains(timestamp_name)) {
        return std::nullopt;
    }

    const json& timestamp = timestamps.at(timestamp_name);
    if (!timestamp.contains("seconds") || !timestamp.contains("nanos")) {
        return std::nullopt;
    }

    return std::make_pair(timestamp.at("seconds").get<long long>(),
                          timestamp.at("nanos").get<int>());
}

std::string timestampToIso8601(long long seconds, int nanos) {
    const std::time_t time = static_cast<std::time_t>(seconds);
    std::tm utc_time{};

#if defined(_WIN32)
    gmtime_s(&utc_time, &time);
#else
    gmtime_r(&time, &utc_time);
#endif

    std::ostringstream out;
    out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%S")
        << "."
        << std::setw(9)
        << std::setfill('0')
        << nanos
        << "Z";
    return out.str();
}

std::string findTimestamp(const json& result, const std::string& signal_path) {
    if (!result.contains("metadata")) {
        throw std::runtime_error("Response message is missing metadata.");
    }

    const auto metadata_node = metadataForPath(result.at("metadata"), signal_path);

    if (!metadata_node.has_value()) {
        throw std::runtime_error("Response metadata is missing signal path: " + signal_path);
    }

    if (auto generated = readTimestamp(*metadata_node, "generated")) {
        return timestampToIso8601(generated->first, generated->second);
    }

    if (auto received = readTimestamp(*metadata_node, "received")) {
        return timestampToIso8601(received->first, received->second);
    }

    throw std::runtime_error("Response metadata is missing timestamp for path: " + signal_path);
}

std::string signalPathFromData(const json& data) {
    if (!data.is_object() || data.empty()) {
        throw std::runtime_error("Response message data must contain one absolute signal path.");
    }

    if (data.size() != 1) {
        throw std::runtime_error(
            "Response message data must contain exactly one absolute signal path.");
    }

    const std::string signal_path = data.begin().key();
    if (signal_path.empty()) {
        throw std::runtime_error("Response message data contains an empty signal path.");
    }

    return signal_path;
}

ShapeRegistry loadRegistry(const std::vector<std::filesystem::path>& shape_paths) {
    const ShaclShapeParser parser;
    return parser.loadFiles(shape_paths);
}

ShapeRegistry loadRegistry(const std::filesystem::path& shape_directory) {
    const ShaclShapeParser parser;
    return parser.loadDirectory(shape_directory);
}

}  // namespace

RdfMapper::RdfMapper(const std::vector<std::filesystem::path>& shape_paths,
                     std::vector<std::string> vehicle_signals,
                     RdfFormat format)
    : registry_(loadRegistry(shape_paths)),
      generator_(registry_),
      writer_(registry_.prefixes),
      format_(format) {
    // Kept for source compatibility; response data paths now select the SHACL shape.
    (void)vehicle_signals;
}

RdfMapper::RdfMapper(const std::filesystem::path& shape_directory,
                     std::vector<std::string> vehicle_signals,
                     RdfFormat format)
    : registry_(loadRegistry(shape_directory)),
      generator_(registry_),
      writer_(registry_.prefixes),
      format_(format) {
    // Kept for source compatibility; response data paths now select the SHACL shape.
    (void)vehicle_signals;
}

std::string RdfMapper::mapMessageToRdf(const std::string& response_message) const {
    const json message = json::parse(response_message);
    if (!message.contains("result") || !message.at("result").is_object()) {
        return "";
    }

    const json& result = message.at("result");
    if (!result.contains("data") || result.at("data").empty()) {
        return "";
    }

    if (!result.contains("instance") || !result.at("instance").is_string()) {
        throw std::runtime_error("Response message is missing result.instance.");
    }

    const std::string signal_path = signalPathFromData(result.at("data"));

    InputMessage input;
    input.values["signal"] = signalNameForPath(kVehicleSchema, signal_path);
    input.values["id"] = result.at("instance").get<std::string>();
    input.values["vehicleId"] = input.values["id"];
    input.values["value"] = findSignalValue(result.at("data"), signal_path);
    input.values["timestamp"] = findTimestamp(result, signal_path);

    const auto triples = generator_.generate(input, format_);
    return writer_.write(triples, format_);
}

}  // namespace rdf_mapper
