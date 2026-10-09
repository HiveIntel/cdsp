#ifndef IL_PROTOCOL_IL_REQUEST_GET_DTO_H
#define IL_PROTOCOL_IL_REQUEST_GET_DTO_H

/**
 * @file il_request_get_dto.h
 * @brief Parameters and JSON serialization for an Information Layer GET request.
 */

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace il_request {

/**
 * @brief Outbound Information Layer GET request parameters.
 *
 * Disengaged optional parameters are omitted, allowing the server's defaults.
 * A present empty string is serialized explicitly. The serializer performs no
 * schema/path validation and does not choose a response shape for the caller.
 */
struct GetRequestDTO {
    /// Schema containing the requested path.
    std::string schema;

    /// Information Layer instance to query.
    std::string instance;

    /// Optional path to query; absence selects the schema root.
    std::optional<std::string> path;

    /// Optional response format, such as `flat` or `nested`.
    std::optional<std::string> format;

    /// Optional response root, such as `absolute` or `relative`.
    std::optional<std::string> root;
};

/**
 * @brief Serializes a GET request DTO as a JSON-RPC 2.0 request.
 * @param json_obj Destination JSON object.
 * @param dto GET request parameters to serialize.
 * @note Replaces the destination value. ADL enables `nlohmann::json message = dto`.
 *       The id is derived from instance/schema/path, not generated per attempt;
 *       repeated requests and requests with different format/root can share an id.
 */
inline void to_json(nlohmann::json& json_obj, const GetRequestDTO& dto) {
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
        {"method", "get"},
        {"id", "get-" + dto.instance + "-" + dto.schema + "_" + dto.path.value_or("")},
        {"params", params_obj}};
}

}  // namespace il_request

#endif  // IL_PROTOCOL_IL_REQUEST_GET_DTO_H
