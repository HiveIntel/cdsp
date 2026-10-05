#ifndef IL_CONNECTION_IL_REQUEST_SET_DTO_H
#define IL_CONNECTION_IL_REQUEST_SET_DTO_H

#include <chrono>
#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "il_data_point_dto.h"

namespace il_request {

/**
 * @brief Outbound Information Layer SET request for one data point.
 *
 * This DTO intentionally supports one leaf value only. Create one request per
 * data point when several values must be written.
 */
struct SetRequestDTO {
    /// Schema containing the data point.
    std::string schema;

    /// Information Layer instance to update.
    std::string instance;

    /// Path and scalar value written by this request.
    il_data_point::DataPointDTO data;

    /// Optional source-generated timestamp for the data point.
    std::optional<std::chrono::system_clock::time_point> generated_timestamp;
};

/**
 * @brief Serializes a single-point SET request as a JSON-RPC 2.0 request.
 * @param json_obj Destination JSON object.
 * @param dto SET request to serialize.
 */
inline void to_json(nlohmann::json& json_obj, const SetRequestDTO& dto) {
    nlohmann::json data_value;
    std::visit([&data_value](const auto& value) { data_value = value; }, dto.data.value);

    nlohmann::json params_obj{{"schema", dto.schema},
                              {"instance", dto.instance},
                              {"path", dto.data.path},
                              {"data", data_value}};

    if (dto.generated_timestamp) {
        const auto epoch = dto.generated_timestamp->time_since_epoch();
        const auto seconds = std::chrono::floor<std::chrono::seconds>(epoch);
        const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - seconds);

        params_obj["metadata"] = {
            {"",
             {{"timestamps",
               {{"generated", {{"seconds", seconds.count()}, {"nanos", nanos.count()}}}}}}},
        };
    }

    json_obj = nlohmann::json{{"jsonrpc", "2.0"},
                              {"method", "set"},
                              {"id",
                               "set-" + dto.instance + "-" + dto.schema + "_" + dto.data.path},
                              {"params", params_obj}};
}

}  // namespace il_request

#endif  // IL_CONNECTION_IL_REQUEST_SET_DTO_H
