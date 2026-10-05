#ifndef IL_CONNECTION_IL_REQUEST_UNSUBSCRIBE_DTO_H
#define IL_CONNECTION_IL_REQUEST_UNSUBSCRIBE_DTO_H

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace il_request {

/**
 * @brief Outbound Information Layer UNSUBSCRIBE request parameters.
 */
struct UnsubscribeRequestDTO {
    /// Schema containing the subscribed path.
    std::string schema;

    /// Information Layer instance currently being observed.
    std::string instance;

    /// Optional path to unsubscribe from; absence selects the schema root.
    std::optional<std::string> path;
};

/**
 * @brief Serializes an UNSUBSCRIBE request DTO as a JSON-RPC 2.0 request.
 * @param json_obj Destination JSON object.
 * @param dto UNSUBSCRIBE request parameters to serialize.
 */
inline void to_json(nlohmann::json& json_obj, const UnsubscribeRequestDTO& dto) {
    nlohmann::json params_obj{{"schema", dto.schema}, {"instance", dto.instance}};
    if (dto.path) {
        params_obj["path"] = *dto.path;
    }

    json_obj = nlohmann::json{{"jsonrpc", "2.0"},
                              {"method", "unsubscribe"},
                              {"id",
                               "unsubscribe-" + dto.instance + "-" + dto.schema + "_" +
                                   dto.path.value_or("")},
                              {"params", params_obj}};
}

}  // namespace il_request

#endif  // IL_CONNECTION_IL_REQUEST_UNSUBSCRIBE_DTO_H
