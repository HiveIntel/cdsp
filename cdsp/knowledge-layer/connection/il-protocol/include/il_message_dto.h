#ifndef IL_PROTOCOL_IL_MESSAGE_DTO_H
#define IL_PROTOCOL_IL_MESSAGE_DTO_H

/**
 * @file il_message_dto.h
 * @brief Owned response envelopes produced by the Information Layer message parser.
 */

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "il_data_point_dto.h"

namespace il_message {

/**
 * @brief Common JSON-RPC fields present in every inbound Information Layer message.
 */
struct ILMessageDTO {
    /// Correlation identifier copied from the response; matching an outbound request is
    /// caller-owned. Empty for an error the server could not tie to a request (null id).
    std::string id;

    /// JSON-RPC protocol version reported by the Information Layer.
    std::string jsonrpc{"2.0"};
};

/**
 * @brief Inbound data response or subscription update.
 *
 * Object payloads are normalized into typed points by `ILMessageParser`.
 * Data and metadata are associated by path rather than vector position. An empty
 * data vector can represent an empty object payload without implying transport failure.
 */
struct DataMessageDTO : ILMessageDTO {
    /// Information Layer instance that produced the data.
    std::string instance;

    /// Scalar data points normalized to dot-separated Information Layer paths.
    std::vector<il_data_point::DataPointDTO> data;

    /// Missing/null wire metadata is absent; an empty object is a present, empty vector.
    std::optional<std::vector<il_data_point::MetadataPointDTO>> metadata;
};

/// Successful acknowledgement without a data payload.
struct StatusMessageDTO : ILMessageDTO {};

/**
 * @brief JSON-RPC error details returned by the Information Layer.
 */
struct ErrorDTO {
    /// JSON-RPC error code.
    std::int64_t code{};

    /// Human-readable error description.
    std::string message;

    /// Arbitrary JSON error details; an explicit wire null differs from an absent field.
    std::optional<nlohmann::json> data;
};

/// Failed JSON-RPC response containing structured error details.
struct ErrorMessageDTO : ILMessageDTO {
    /// Owned details from the JSON-RPC error object.
    ErrorDTO error;
};

/// Valid inbound message categories recognized by the Information Layer parser.
using InboundMessageDTO = std::variant<DataMessageDTO, StatusMessageDTO, ErrorMessageDTO>;

/// Writes diagnostic text rather than a JSON-RPC wire envelope.
inline std::ostream& operator<<(std::ostream& out, const ILMessageDTO& dto) {
    return out << "ILMessageDTO{id=" << dto.id << ", jsonrpc=" << dto.jsonrpc << '}';
}

/// Writes a human-readable data message DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const DataMessageDTO& dto) {
    out << "DataMessageDTO{id=" << dto.id << ", jsonrpc=" << dto.jsonrpc
        << ", instance=" << dto.instance << ", data=[";
    for (std::size_t index = 0; index < dto.data.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << dto.data[index];
    }
    out << "], metadata=";
    if (!dto.metadata.has_value()) {
        return out << "<absent>}";
    }

    out << '[';
    for (std::size_t index = 0; index < dto.metadata->size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << dto.metadata->at(index);
    }
    return out << "]}";
}

/// Writes a human-readable status message DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const StatusMessageDTO& dto) {
    return out << "StatusMessageDTO{id=" << dto.id << ", jsonrpc=" << dto.jsonrpc << '}';
}

/// Writes human-readable JSON-RPC error details to a stream.
inline std::ostream& operator<<(std::ostream& out, const ErrorDTO& dto) {
    out << "ErrorDTO{code=" << dto.code << ", message=" << dto.message << ", data=";
    return out << (dto.data ? dto.data->dump() : "<absent>") << '}';
}

/// Writes a human-readable error message DTO to a stream.
inline std::ostream& operator<<(std::ostream& out, const ErrorMessageDTO& dto) {
    return out << "ErrorMessageDTO{id=" << dto.id << ", jsonrpc=" << dto.jsonrpc
               << ", error=" << dto.error << '}';
}

}  // namespace il_message

#endif  // IL_PROTOCOL_IL_MESSAGE_DTO_H
