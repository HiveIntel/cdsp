#include "il_request/unsubscribe_request.h"

#include <utility>

namespace il_request {

UnsubscribeRequest::UnsubscribeRequest(std::string instance, Node node, std::string schema)
    : Request(std::move(instance), node.getName(), std::move(schema)), node_(std::move(node)) {}

UnsubscribeRequest::UnsubscribeRequest(std::string instance,
                                       std::string root_request_id,
                                       std::string schema)
    : Request(std::move(instance), std::move(root_request_id), std::move(schema)) {}

const std::optional<Node>& UnsubscribeRequest::getNode() const noexcept { return node_; }

std::string_view UnsubscribeRequest::getMethod() const noexcept { return "unsubscribe"; }

std::ostream& operator<<(std::ostream& os, const UnsubscribeRequest& request) {
    os << "UnsubscribeRequest { id: " << request.getRequestId()
       << ", instance: " << request.getInstance()
       << ", path: " << (request.getNode() ? request.getNode()->getName() : "<root>") << " }";
    return os;
}

nlohmann::json to_json(const UnsubscribeRequest& request) {
    nlohmann::json params = {
        {"schema", request.getSchema()},
        {"instance", request.getInstance()},
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
