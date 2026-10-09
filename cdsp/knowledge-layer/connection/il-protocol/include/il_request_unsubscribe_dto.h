#ifndef IL_PROTOCOL_IL_REQUEST_UNSUBSCRIBE_DTO_H
#define IL_PROTOCOL_IL_REQUEST_UNSUBSCRIBE_DTO_H

/**
 * @file il_request_unsubscribe_dto.h
 * @brief Parameters and JSON serialization for an Information Layer UNSUBSCRIBE request.
 */

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace il_request {

/**
 * @brief Outbound Information Layer UNSUBSCRIBE request parameters.
 *
 * Targets a schema/instance/path, not an opaque subscription handle. The serializer
 * performs no validation and does not track whether a matching subscription exists.
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
 * @note Replaces the destination value through nlohmann ADL serialization. The id is
 *       deterministic for instance/schema/path; repeated requests reuse it. A missing
 *       path is omitted, whereas an engaged empty path is serialized explicitly.
 */
inline void to_json(nlohmann::json& json_obj, const UnsubscribeRequestDTO& dto) {
    nlohmann::json params_obj{{"schema", dto.schema}, {"instance", dto.instance}};
    if (dto.path) {
        params_obj["path"] = *dto.path;
    }

    json_obj = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"method", "unsubscribe"},
        {"id", "unsubscribe-" + dto.instance + "-" + dto.schema + "_" + dto.path.value_or("")},
        {"params", params_obj}};
}

}  // namespace il_request

#endif  // IL_PROTOCOL_IL_REQUEST_UNSUBSCRIBE_DTO_H
