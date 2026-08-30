#ifndef IL_REQUEST_INBOUND_MESSAGE_DTO_H
#define IL_REQUEST_INBOUND_MESSAGE_DTO_H

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

namespace il_request {

// JSON-RPC permits a string, integer, or null ID in an IL response.
using MessageId = std::variant<std::string, std::int64_t, std::nullptr_t>;

struct DataMessageDto {
    MessageId id;
    std::string instance;
    nlohmann::json data;
    std::optional<nlohmann::json> metadata;
};

struct StatusErrorDto {
    std::int64_t code;
    std::string message;
    std::optional<nlohmann::json> data;
};

struct StatusMessageDto {
    MessageId id;
    std::optional<StatusErrorDto> error;

    [[nodiscard]] bool isSuccess() const noexcept { return !error.has_value(); }
};

using InboundMessageDto = std::variant<DataMessageDto, StatusMessageDto>;

}  // namespace il_request

#endif  // IL_REQUEST_INBOUND_MESSAGE_DTO_H
