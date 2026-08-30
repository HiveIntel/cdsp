#include "il_request/subscribe_request.h"

#include <utility>

namespace il_request {

SubscribeRequest::SubscribeRequest(std::string instance,
                                   Node node,
                                   ResponseOptions options,
                                   std::string schema)
    : Request(std::move(instance), node.getName(), std::move(schema)),
      node_(std::move(node)),
      options_(options) {}

SubscribeRequest::SubscribeRequest(std::string instance,
                                   std::string root_request_id,
                                   ResponseOptions options,
                                   std::string schema)
    : Request(std::move(instance), std::move(root_request_id), std::move(schema)), options_(options) {}

const std::optional<Node>& SubscribeRequest::getNode() const noexcept { return node_; }

ResponseOptions SubscribeRequest::getResponseOptions() const noexcept { return options_; }

std::string_view SubscribeRequest::getMethod() const noexcept { return "subscribe"; }

std::ostream& operator<<(std::ostream& os, const SubscribeRequest& request) {
    os << "SubscribeRequest { id: " << request.getRequestId()
       << ", instance: " << request.getInstance()
       << ", path: " << (request.getNode() ? request.getNode()->getName() : "<root>") << " }";
    return os;
}

nlohmann::json to_json(const SubscribeRequest& request) {
    nlohmann::json params = {
        {"schema", request.getSchema()},
        {"instance", request.getInstance()},
        {"format", std::string{to_string(request.getResponseOptions().format)}},
        {"root", std::string{to_string(request.getResponseOptions().root)}},
    };
    if (request.getNode()) {
        params["path"] = request.getNode()->getName();
    }
    return {
        {"jsonrpc", "2.0"},
        {"method", std::string{request.getMethod()}},
        {"id", request.getRequestId()},
        {"params", std::move(params)},
    };
}

}  // namespace il_request
