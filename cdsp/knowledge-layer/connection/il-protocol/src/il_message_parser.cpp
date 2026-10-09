#include "il_message_parser.h"

/**
 * @file il_message_parser.cpp
 * @brief Stateless decoding of Information Layer JSON-RPC response payloads.
 */

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace il_message {

namespace {

using json = nlohmann::json;
using il_data_point::DataPointDTO;
using il_data_point::MetadataPointDTO;
using il_data_point::TimestampsDTO;

/// Borrows a required object-valued member; the parent JSON must outlive the reference.
const json& requireObjectMember(const json& object, const char* name) {
    if (!object.contains(name) || !object.at(name).is_object()) {
        throw std::invalid_argument(std::string{"IL response requires object member: "} + name);
    }
    return object.at(name);
}

/// Copies a string correlation id; this parser neither tracks requests nor matches replies.
std::string parseId(const json& message) {
    if (!message.contains("id") || !message.at("id").is_string()) {
        throw std::invalid_argument("IL response requires a string id");
    }
    return message.at("id").get<std::string>();
}

/// Joins object keys with dots; an empty key denotes the current path, not a new segment.
std::string childPath(const std::string& parent, const std::string& child) {
    if (child.empty()) {
        return parent;
    }
    if (parent.empty()) {
        return child;
    }
    return parent + "." + child;
}

/// Accepts signed/unsigned JSON integers only when representable as an int64_t.
/// The field description is used in validation diagnostics, not as a lookup key.
std::int64_t parseInteger(const json& value, const std::string& field) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument(field + " exceeds the signed 64-bit range");
        }
        return static_cast<std::int64_t>(number);
    }
    if (!value.is_number_integer()) {
        throw std::invalid_argument(field + " must be an integer");
    }
    return value.get<std::int64_t>();
}

/// Owns a supported scalar value and its inferred path; arrays and null are rejected.
/// A relative root leaf needs request context, which this parser does not retain.
DataPointDTO parseDataPoint(const std::string& path, const json& value) {
    if (path.empty()) {
        throw std::invalid_argument("IL data response cannot infer a path for a scalar value");
    }

    if (value.is_string()) {
        return {path, value.get<std::string>()};
    }
    if (value.is_boolean()) {
        return {path, value.get<bool>()};
    }
    if (value.is_number_unsigned()) {
        return {path, parseInteger(value, "IL data point '" + path + "'")};
    }
    if (value.is_number_integer()) {
        return {path, value.get<std::int64_t>()};
    }
    if (value.is_number_float()) {
        return {path, value.get<double>()};
    }

    throw std::invalid_argument("IL data point '" + path + "' must contain a scalar value");
}

/// Recursively flattens nested objects; already-dotted keys are retained as supplied.
/// Empty objects contribute no points; output order is JSON iteration order, not wire order.
void appendDataPoints(const json& value, const std::string& path,
                      std::vector<DataPointDTO>& points) {
    if (!value.is_object()) {
        points.push_back(parseDataPoint(path, value));
        return;
    }

    for (auto member = value.cbegin(); member != value.cend(); ++member) {
        appendDataPoints(member.value(), childPath(path, member.key()), points);
    }
}

/// Decodes seconds/nanos since the system clock epoch; nanos must be in [0, 1e9).
std::chrono::system_clock::time_point parseTimePoint(const json& value, const std::string& field) {
    if (!value.is_object() || !value.contains("seconds") || !value.contains("nanos")) {
        throw std::invalid_argument(field + " must contain integer seconds and nanos");
    }

    const std::int64_t seconds = parseInteger(value.at("seconds"), field + ".seconds");
    const std::int64_t nanos = parseInteger(value.at("nanos"), field + ".nanos");
    if (nanos < 0 || nanos >= 1000000000) {
        throw std::invalid_argument(field + ".nanos must be between 0 and 999999999");
    }

    using ClockDuration = std::chrono::system_clock::duration;
    const auto min_seconds = std::chrono::duration_cast<std::chrono::seconds>(ClockDuration::min());
    const auto max_seconds = std::chrono::duration_cast<std::chrono::seconds>(ClockDuration::max());
    if (seconds < min_seconds.count() || seconds > max_seconds.count()) {
        throw std::invalid_argument(field + " is outside the system clock range");
    }

    const auto clock_seconds =
        std::chrono::duration_cast<ClockDuration>(std::chrono::seconds{seconds});
    const auto clock_nanos =
        std::chrono::duration_cast<ClockDuration>(std::chrono::nanoseconds{nanos});
    if (clock_seconds > ClockDuration::max() - clock_nanos) {
        throw std::invalid_argument(field + " is outside the system clock range");
    }
    return std::chrono::system_clock::time_point{clock_seconds + clock_nanos};
}

/// Decodes optional received/generated fields; absent fields remain disengaged optionals.
/// Unrecognized timestamp fields are ignored.
TimestampsDTO parseTimestamps(const json& value, const std::string& path) {
    if (!value.is_object()) {
        throw std::invalid_argument("IL metadata timestamps for '" + path + "' must be an object");
    }

    TimestampsDTO timestamps;
    if (value.contains("received")) {
        timestamps.received =
            parseTimePoint(value.at("received"), "IL metadata '" + path + "'.timestamps.received");
    }
    if (value.contains("generated")) {
        timestamps.generated = parseTimePoint(value.at("generated"),
                                              "IL metadata '" + path + "'.timestamps.generated");
    }
    return timestamps;
}

/// Collects timestamp-bearing metadata objects using the same dot-path join as data.
/// Non-object leaves are ignored; a timestamps member ends recursion at that node.
void appendMetadataPoints(const json& value, const std::string& path,
                          std::vector<MetadataPointDTO>& points) {
    if (!value.is_object()) {
        return;
    }

    if (value.contains("timestamps")) {
        points.push_back({path, parseTimestamps(value.at("timestamps"), path)});
        return;
    }

    for (auto member = value.cbegin(); member != value.cend(); ++member) {
        appendMetadataPoints(member.value(), childPath(path, member.key()), points);
    }
}

/// Owns error details and preserves optional error.data, including an explicit JSON null.
ErrorMessageDTO parseError(const json& message, std::string id) {
    const json& error = requireObjectMember(message, "error");

    if (!error.contains("code") || !error.at("code").is_number_integer()) {
        throw std::invalid_argument("IL error response requires an integer error.code");
    }

    if (!error.contains("message") || !error.at("message").is_string()) {
        throw std::invalid_argument("IL error response requires a string error.message");
    }

    ErrorDTO error_dto{
        parseInteger(error.at("code"), "IL error response error.code"),
        error.at("message").get<std::string>(),
        std::nullopt,
    };

    if (error.contains("data")) {
        error_dto.data = error.at("data");
    }

    return {ILMessageDTO{std::move(id), "2.0"}, std::move(error_dto)};
}

/// Normalizes one object-shaped data payload and optional timestamp metadata.
/// The caller has already established that result contains a data member.
DataMessageDTO parseData(const json& result, std::string id) {
    if (!result.contains("instance") || !result.at("instance").is_string()) {
        throw std::invalid_argument("IL data response requires string result.instance");
    }

    if (!result.at("data").is_object()) {
        throw std::invalid_argument("IL data response requires object result.data");
    }

    std::vector<DataPointDTO> data_points;
    appendDataPoints(result.at("data"), "", data_points);

    DataMessageDTO data_message{ILMessageDTO{std::move(id), "2.0"},
                                result.at("instance").get<std::string>(), std::move(data_points),
                                std::nullopt};

    if (result.contains("metadata") && !result.at("metadata").is_null()) {
        if (!result.at("metadata").is_object()) {
            throw std::invalid_argument("IL data response metadata must be an object or null");
        }
        std::vector<MetadataPointDTO> metadata_points;
        appendMetadataPoints(result.at("metadata"), "", metadata_points);
        data_message.metadata = std::move(metadata_points);
    }
    return data_message;
}

}  // namespace

InboundMessageDTO ILMessageParser::parse(const json& message) {
    if (!message.is_object()) {
        throw std::invalid_argument("IL response must be a JSON object");
    }

    if (!message.contains("jsonrpc") || message.at("jsonrpc") != "2.0") {
        throw std::invalid_argument("IL response requires jsonrpc: 2.0");
    }

    // Classify the envelope before decoding payloads; a reply cannot be both result and error.
    std::string id = parseId(message);
    if (message.contains("error")) {
        if (message.contains("result")) {
            throw std::invalid_argument("IL response cannot contain both result and error");
        }
        return parseError(message, std::move(id));
    }

    const json& result = requireObjectMember(message, "result");
    if (result.contains("data")) {
        return parseData(result, std::move(id));
    }

    // Only an empty result is an acknowledgement; do not silently discard an unknown payload.
    if (!result.empty()) {
        throw std::invalid_argument("IL status response requires an empty result object");
    }

    return StatusMessageDTO{ILMessageDTO{std::move(id), "2.0"}};
}

InboundMessageDTO ILMessageParser::parseWire(std::string_view message) {
    // Decoding owns the JSON tree; returned DTOs own their values beyond this temporary's lifetime.
    return parse(json::parse(message.begin(), message.end()));
}

}  // namespace il_message
