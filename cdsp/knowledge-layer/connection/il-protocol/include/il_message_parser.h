#ifndef IL_PROTOCOL_IL_MESSAGE_PARSER_H
#define IL_PROTOCOL_IL_MESSAGE_PARSER_H

/**
 * @file il_message_parser.h
 * @brief Transport-independent decoder for supported Information Layer response envelopes.
 */

#include <nlohmann/json.hpp>
#include <string_view>

#include "il_message_dto.h"

namespace il_message {

/**
 * @brief Parses inbound Information Layer JSON-RPC messages into typed DTOs.
 *
 * Supported envelopes are a data response/update containing `result.data`, a
 * successful acknowledgement containing an empty `result` object, and a
 * JSON-RPC `error` response. Every envelope must use JSON-RPC 2.0 and a string
 * request identifier.
 *
 * The parser is stateless and retains no references to its input. Independent
 * calls may execute concurrently while callers keep their JSON inputs unchanged.
 * Request correlation, response ordering, and subscriptions belong to the caller.
 *
 * @note Paths are flattened as supplied, without restoring a schema or request-path
 *       prefix. Scalar data and an empty-path scalar leaf are unsupported, so some
 *       relative root-leaf responses require context that this parser does not accept.
 */
class ILMessageParser {
   public:
    /**
     * @brief Validates and classifies a decoded JSON-RPC message.
     * @param message Decoded JSON value received from the Information Layer.
     *
     * The parser normalizes object-shaped `result.data` and optional
     * `result.metadata` into typed path/value and path/timestamp points.
     * Optional `error.data` remains JSON.
     * Callers must associate metadata with data by path, not vector index.
     * Absent/null metadata yields no metadata value; an empty metadata object
     * yields an empty vector.
     *
     * @return The corresponding `DataMessageDTO`, `StatusMessageDTO`, or
     *         `ErrorMessageDTO` variant.
     * @throws std::invalid_argument if the message does not match the supported
     *         Information Layer response contract.
     */
    [[nodiscard]] static InboundMessageDTO parse(const nlohmann::json& message);

    /**
     * @brief Decodes, validates, and classifies a JSON-RPC wire message.
     * @param message UTF-8 JSON text received from the Information Layer.
     * @return The corresponding `DataMessageDTO`, `StatusMessageDTO`, or
     *         `ErrorMessageDTO` variant.
     * @throws nlohmann::json::parse_error if the input is not valid JSON.
     * @throws std::invalid_argument if the decoded message does not match the
     *         supported Information Layer response contract.
     */
    [[nodiscard]] static InboundMessageDTO parseWire(std::string_view message);
};

}  // namespace il_message

#endif  // IL_PROTOCOL_IL_MESSAGE_PARSER_H
