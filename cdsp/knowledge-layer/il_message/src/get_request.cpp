#include "il_message/get_request.h"

#include <utility>

namespace il_message {

GetRequest::GetRequest(std::string instance, Node node, ResponseOptions options, std::string schema)
    : Request(std::move(instance), node.getName(), std::move(schema)),
      node_(std::move(node)),
      options_(options) {}

GetRequest::GetRequest(std::string instance,
                       std::string root_request_id,
                       ResponseOptions options,
                       std::string schema)
    : Request(std::move(instance), std::move(root_request_id), std::move(schema)), options_(options) {}

const std::optional<Node>& GetRequest::getNode() const noexcept { return node_; }

ResponseOptions GetRequest::getResponseOptions() const noexcept { return options_; }

std::string_view GetRequest::getMethod() const noexcept { return "get"; }

std::ostream& operator<<(std::ostream& os, const GetRequest& request) {
    os << "GetRequest { id: " << request.getRequestId()
       << ", instance: " << request.getInstance()
       << ", path: " << (request.getNode() ? request.getNode()->getName() : "<root>") << " }";
    return os;
}

nlohmann::json to_json(const GetRequest& request) {
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

}  // namespace il_message
