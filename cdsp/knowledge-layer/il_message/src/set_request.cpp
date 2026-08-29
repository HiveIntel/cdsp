#include "il_message/set_request.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace il_message {

namespace {

void writeTimestamp(nlohmann::json& destination,
                    const std::chrono::system_clock::time_point& timestamp) {
    const auto epoch = timestamp.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - seconds);
    destination = {{"seconds", seconds.count()}, {"nanos", nanos.count()}};
}

void writeNodeMetadata(nlohmann::json& metadata, const Node& node) {
    const auto& node_metadata = node.getMetadata();
    if (node_metadata.timestamps.received) {
        writeTimestamp(metadata[node.getName()]["timestamps"]["received"],
                       *node_metadata.timestamps.received);
    }
    if (node_metadata.timestamps.generated) {
        writeTimestamp(metadata[node.getName()]["timestamps"]["generated"],
                       *node_metadata.timestamps.generated);
    }
    if (node_metadata.origin && node_metadata.origin->name) {
        metadata[node.getName()]["origin"]["type"]["name"] = *node_metadata.origin->name;
        metadata[node.getName()]["origin"]["type"]["uri"] =
            node_metadata.origin->uri.value_or("");
    }
    if (node_metadata.confidence) {
        metadata[node.getName()]["confidence"] = {
            {"type", std::string{to_string(node_metadata.confidence->type)}},
            {"value", node_metadata.confidence->value},
        };
    }
}

}  // namespace

std::string_view to_string(SyncMode sync_mode) noexcept {
    switch (sync_mode) {
        case SyncMode::OFF:
            return "off";
        case SyncMode::LOCAL:
            return "local";
        case SyncMode::REMOTE:
            return "remote";
    }
    return "off";
}

SetRequest::SetRequest(std::string instance, Node node, SetOptions options, std::string schema)
    : Request(std::move(instance), node.getName(), std::move(schema)),
      nodes_{std::move(node)},
      options_(std::move(options)) {}

SetRequest::SetRequest(std::string instance,
                       std::vector<Node> nodes,
                       std::string batch_request_id,
                       SetOptions options,
                       std::string schema)
    : Request(std::move(instance), std::move(batch_request_id), std::move(schema)),
      nodes_(std::move(nodes)),
      options_(std::move(options)) {
    if (nodes_.empty()) {
        throw std::invalid_argument("SetRequest requires at least one node");
    }
}

const std::vector<Node>& SetRequest::getNodes() const noexcept { return nodes_; }

const SetOptions& SetRequest::getOptions() const noexcept { return options_; }

std::string_view SetRequest::getMethod() const noexcept { return "set"; }

std::ostream& operator<<(std::ostream& os, const SetRequest& request) {
    os << "SetRequest { id: " << request.getRequestId()
       << ", instance: " << request.getInstance()
       << ", nodes: " << request.getNodes().size() << " }";
    return os;
}

nlohmann::json to_json(const SetRequest& request) {
    nlohmann::json data = nlohmann::json::object();
    nlohmann::json metadata = nlohmann::json::object();

    for (const auto& node : request.getNodes()) {
        if (!node.getValue()) {
            data[node.getName()] = "";
        } else {
            try {
                data[node.getName()] = nlohmann::json::parse(*node.getValue());
            } catch (const nlohmann::json::exception&) {
                data[node.getName()] = *node.getValue();
            }
        }
        writeNodeMetadata(metadata, node);
    }

    nlohmann::json params = {
        {"schema", request.getSchema()},
        {"instance", request.getInstance()},
        {"data", std::move(data)},
        {"metadata", std::move(metadata)},
    };
    if (request.getOptions().path) {
        params["path"] = *request.getOptions().path;
    }
    if (request.getOptions().sync) {
        params["sync"] = std::string{to_string(*request.getOptions().sync)};
    }
    if (request.getOptions().timeseries) {
        params["timeseries"] = *request.getOptions().timeseries;
    }

    return {
        {"jsonrpc", "2.0"},
        {"method", std::string{request.getMethod()}},
        {"id", request.getRequestId()},
        {"params", std::move(params)},
    };
}

}  // namespace il_message
