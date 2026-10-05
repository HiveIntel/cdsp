#ifndef IL_CONNECTION_IL_DATA_POINT_DTO_H
#define IL_CONNECTION_IL_DATA_POINT_DTO_H

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
 * corresponding value.
 */
struct TimeStampDTO {
    /// Time at which the Information Layer received the data point.
    std::optional<std::chrono::system_clock::time_point> received;

    /// Time at which the source generated the data point.
    std::optional<std::chrono::system_clock::time_point> generated;
};

/**
 * @brief Metadata belonging to one Information Layer path.
 * TODO: Consider to ask the Information Layer to embed the Timestamps directly into the
 * DataPointDTO, so that we can remove this struct and simplify the code. And the path is always
 * flat and being repeated.
 *
 */
struct MetaDataPointDTO {
    /// Information Layer path associated with the metadata.
    std::string path;

    /// Timestamps reported for the path.
    TimeStampDTO timestamp;
};

/**
 * @brief Value belonging to one Information Layer path.
 */
struct DataPointDTO {
    /// Information Layer path associated with the value.
    std::string path;

    /// Scalar value supported by the data-point representation.
    std::variant<std::string, std::int64_t, double, bool> value;
};

/// Writes a human-readable timestamp DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const TimeStampDTO& dto) {
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

    out << "TimeStampDTO{received=";
    write_time(dto.received);
    out << ", generated=";
    write_time(dto.generated);
    return out << '}';
}

/// Writes a human-readable metadata-point DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const MetaDataPointDTO& dto) {
    return out << "MetaDataPointDTO{path=" << dto.path << ", timestamp=" << dto.timestamp << '}';
}

/// Writes a human-readable data-point DTO to a stream.
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

#endif  // IL_CONNECTION_IL_DATA_POINT_DTO_H
