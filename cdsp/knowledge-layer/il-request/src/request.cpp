#include "il_request/request.h"

#include <stdexcept>
#include <utility>

namespace il_request {

std::string_view to_string(ResponseFormat format) noexcept {
    return format == ResponseFormat::FLAT ? "flat" : "nested";
}

std::string_view to_string(ResponseRoot root) noexcept {
    return root == ResponseRoot::ABSOLUTE ? "absolute" : "relative";
}

Request::Request(std::string instance, std::string request_id, std::string schema)
    : instance_(std::move(instance)), request_id_(std::move(request_id)), schema_(std::move(schema)) {
    if (instance_.empty()) {
        throw std::invalid_argument("Request instance cannot be empty");
    }
    if (request_id_.empty()) {
        throw std::invalid_argument("Request ID cannot be empty");
    }
    if (schema_.empty()) {
        throw std::invalid_argument("Request schema cannot be empty");
    }
}

const std::string& Request::getInstance() const noexcept { return instance_; }

const std::string& Request::getSchema() const noexcept { return schema_; }

const std::string& Request::getRequestId() const noexcept { return request_id_; }

}  // namespace il_request
