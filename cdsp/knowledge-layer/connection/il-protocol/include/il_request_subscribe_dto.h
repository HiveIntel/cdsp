#ifndef IL_PROTOCOL_IL_REQUEST_SUBSCRIBE_DTO_H
#define IL_PROTOCOL_IL_REQUEST_SUBSCRIBE_DTO_H

/**
 * @file il_request_subscribe_dto.h
 * @brief Parameters and JSON serialization for an Information Layer SUBSCRIBE request.
 */

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace il_request {

/**
 * @brief Outbound Information Layer SUBSCRIBE request parameters.
 *
 * Defaults select the Vehicle schema with flat, absolute-root updates. Reset an
 * optional to omit it; an engaged empty string is transmitted explicitly. The
 * serializer does not establish a subscription or validate server-side parameters.
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
 * @note Replaces the destination value through nlohmann ADL serialization. The id
 *       identifies instance/schema/path rather than a unique subscription attempt;
 *       subscription lifetime and response correlation belong to the caller.
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

    json_obj = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"method", "subscribe"},
        {"id", "subscribe-" + dto.instance + "-" + dto.schema + "_" + dto.path.value_or("")},
        {"params", params_obj}};
}

}  // namespace il_request

#endif  // IL_PROTOCOL_IL_REQUEST_SUBSCRIBE_DTO_H
