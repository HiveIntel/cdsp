#ifndef IL_MESSAGE_INBOUND_MESSAGE_PARSER_H
#define IL_MESSAGE_INBOUND_MESSAGE_PARSER_H

#include <string_view>

#include <nlohmann/json.hpp>

#include "il_request/inbound_message_dto.h"

namespace il_request {

// Parses the IL JSON-RPC response envelope. It intentionally preserves data
// JSON: the originating request context is needed to interpret format/root.
class InboundMessageParser {
   public:
    [[nodiscard]] static InboundMessageDto parse(const nlohmann::json& message);
    [[nodiscard]] static InboundMessageDto parseWire(std::string_view message);
};

}  // namespace il_request

#endif  // IL_MESSAGE_INBOUND_MESSAGE_PARSER_H
