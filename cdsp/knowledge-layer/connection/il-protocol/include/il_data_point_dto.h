#ifndef IL_PROTOCOL_IL_DATA_POINT_DTO_H
#define IL_PROTOCOL_IL_DATA_POINT_DTO_H

/**
 * @file il_data_point_dto.h
 * @brief Owned scalar values and timestamp metadata at the Information Layer boundary.
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <type_traits>
#include <variant>

namespace il_data_point {

/**
 * @brief Timestamps associated with an Information Layer data point.
 *
 * A timestamp is absent when the Information Layer did not provide the
 * corresponding value. Time points use the system clock; diagnostic stream
 * output displays milliseconds since its epoch, not an ISO-8601 string.
 */
struct TimestampsDTO {
    /// Time at which the Information Layer received the data point.
    std::optional<std::chrono::system_clock::time_point> received;

    /// Time at which the source generated the data point.
    std::optional<std::chrono::system_clock::time_point> generated;
};

/**
 * @brief Metadata belonging to one Information Layer path.
 *
 * Match metadata to data by path; the two vectors need not have the same size or order.
 * An empty metadata path denotes the response root.
 *
 * TODO: Discuss embedding timestamps in DataPointDTO with the Information Layer
 * contract owner before consolidating these separate representations.
 */
struct MetadataPointDTO {
    /// Information Layer path associated with the metadata.
    std::string path;

    /// Optional received/generated timestamps reported for this path.
    TimestampsDTO timestamps;
};

/**
 * @brief Value belonging to one Information Layer path.
 *
 * Values own their storage after parsing. A path is preserved relative to the
 * supplied response shape; it does not necessarily include the schema prefix.
 */
struct DataPointDTO {
    /// Information Layer path associated with the value.
    std::string path;

    /// Supported scalar types; null, arrays, objects, and integers above INT64_MAX are unsupported.
    std::variant<std::string, std::int64_t, double, bool> value;
};

/// Writes a human-readable timestamp DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const TimestampsDTO& dto) {
    const auto write_time =
        [&out](const std::optional<std::chrono::system_clock::time_point>& time) -> std::ostream& {
        if (!time.has_value()) {
            return out << "<absent>";
        }
        return out << std::chrono::duration_cast<std::chrono::milliseconds>(
                          time->time_since_epoch())
                          .count()
                   << "ms";
    };

    out << "TimestampsDTO{received=";
    write_time(dto.received);
    out << ", generated=";
    write_time(dto.generated);
    return out << '}';
}

/// Writes a human-readable metadata-point DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const MetadataPointDTO& dto) {
    return out << "MetadataPointDTO{path=" << dto.path << ", timestamps=" << dto.timestamps << '}';
}

/// Writes diagnostic text, not JSON; string values are quoted but not escaped.
inline std::ostream& operator<<(std::ostream& out, const DataPointDTO& dto) {
    out << "DataPointDTO{path=" << dto.path << ", value=";
    std::visit(
        [&out](const auto& value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, std::string>) {
                out << '"' << value << '"';
            } else if constexpr (std::is_same_v<Value, bool>) {
                out << (value ? "true" : "false");
            } else {
                out << value;
            }
        },
        dto.value);
    return out << '}';
}

}  // namespace il_data_point

#endif  // IL_PROTOCOL_IL_DATA_POINT_DTO_H
