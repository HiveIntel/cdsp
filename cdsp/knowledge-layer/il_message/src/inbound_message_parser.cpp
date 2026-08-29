#include "il_message/inbound_message_parser.h"

#include <stdexcept>
#include <string>

namespace il_message {

namespace {

using json = nlohmann::json;

const json& requireObjectMember(const json& object, const char* name) {
    if (!object.contains(name) || !object.at(name).is_object()) {
        throw std::invalid_argument(std::string{"IL response requires object member: "} + name);
    }
    return object.at(name);
}

MessageId parseId(const json& message) {
    if (!message.contains("id")) {
        throw std::invalid_argument("IL response requires an id");
    }

    const json& id = message.at("id");
    if (id.is_string()) {
        return id.get<std::string>();
    }
    if (id.is_number_integer()) {
        return id.get<std::int64_t>();
    }
    if (id.is_null()) {
        return nullptr;
    }
    throw std::invalid_argument("IL response id must be a string, integer, or null");
}

StatusMessageDto parseError(const json& message, MessageId id) {
    const json& error = requireObjectMember(message, "error");
    if (!error.contains("code") || !error.at("code").is_number_integer()) {
        throw std::invalid_argument("IL error response requires an integer error.code");
    }
    if (!error.contains("message") || !error.at("message").is_string()) {
        throw std::invalid_argument("IL error response requires a string error.message");
    }

    StatusErrorDto status_error{
        error.at("code").get<std::int64_t>(),
        error.at("message").get<std::string>(),
        std::nullopt,
    };
    if (error.contains("data")) {
        status_error.data = error.at("data");
    }
    return {std::move(id), std::move(status_error)};
}

DataMessageDto parseData(const json& result, MessageId id) {
    if (!result.contains("instance") || !result.at("instance").is_string()) {
        throw std::invalid_argument("IL data response requires string result.instance");
    }

    DataMessageDto data_message{
        std::move(id),
        result.at("instance").get<std::string>(),
        result.at("data"),
        std::nullopt,
    };
    if (result.contains("metadata") && !result.at("metadata").is_null()) {
        data_message.metadata = result.at("metadata");
    }
    return data_message;
}

}  // namespace

InboundMessageDto InboundMessageParser::parse(const json& message) {
    if (!message.is_object()) {
        throw std::invalid_argument("IL response must be a JSON object");
    }
    if (!message.contains("jsonrpc") || message.at("jsonrpc") != "2.0") {
        throw std::invalid_argument("IL response requires jsonrpc: 2.0");
    }

    MessageId id = parseId(message);
    if (message.contains("error")) {
        if (message.contains("result")) {
            throw std::invalid_argument("IL response cannot contain both result and error");
        }
        return parseError(message, std::move(id));
    }

    const json& result = requireObjectMember(message, "result");
    if (result.contains("data")) {
        return parseData(result, std::move(id));
    }

    // The deployed IL uses result: {} for a successful SET/SUBSCRIBE/UNSUBSCRIBE acknowledgement.
    return StatusMessageDto{std::move(id), std::nullopt};
}

InboundMessageDto InboundMessageParser::parseWire(std::string_view message) {
    return parse(json::parse(message.begin(), message.end()));
}

}  // namespace il_message
