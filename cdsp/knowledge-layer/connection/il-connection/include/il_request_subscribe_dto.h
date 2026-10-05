#ifndef IL_CONNECTION_IL_REQUEST_SUBSCRIBE_DTO_H
#define IL_CONNECTION_IL_REQUEST_SUBSCRIBE_DTO_H

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace il_request {

/**
 * @brief Outbound Information Layer SUBSCRIBE request parameters.
 */
struct SubscribeRequestDTO {
    /// Schema containing the path to subscribe to.
    std::string schema{"Vehicle"};

    /// Information Layer instance to observe.
    std::string instance;

    /// Optional path to subscribe to; absence selects the schema root.
    std::optional<std::string> path;

    /// Optional update format, such as `flat` or `nested`.
    std::optional<std::string> format{"flat"};

    /// Optional update root, such as `absolute` or `relative`.
    std::optional<std::string> root{"absolute"};
};

/**
 * @brief Serializes a SUBSCRIBE request DTO as a JSON-RPC 2.0 request.
 * @param json_obj Destination JSON object.
 * @param dto SUBSCRIBE request parameters to serialize.
 */
inline void to_json(nlohmann::json& json_obj, const SubscribeRequestDTO& dto) {
    nlohmann::json params_obj{{"schema", dto.schema}, {"instance", dto.instance}};
    if (dto.path) {
        params_obj["path"] = *dto.path;
    }
    if (dto.format) {
        params_obj["format"] = *dto.format;
    }
    if (dto.root) {
        params_obj["root"] = *dto.root;
    }

    json_obj = nlohmann::json{{"jsonrpc", "2.0"},
                              {"method", "subscribe"},
                              {"id",
                               "subscribe-" + dto.instance + "-" + dto.schema + "_" +
                                   dto.path.value_or("")},
                              {"params", params_obj}};
}

}  // namespace il_request

#endif  // IL_CONNECTION_IL_REQUEST_SUBSCRIBE_DTO_H
