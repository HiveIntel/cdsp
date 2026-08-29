#include "il_message/node.h"

#include <stdexcept>
#include <utility>

namespace il_message {

Node::Node(std::string name, std::optional<std::string> value, Metadata metadata)
    : name_(std::move(name)), value_(std::move(value)), metadata_(std::move(metadata)) {
    if (name_.empty()) {
        throw std::invalid_argument("Node name cannot be empty");
    }
}

const std::string& Node::getName() const noexcept { return name_; }

const std::optional<std::string>& Node::getValue() const noexcept { return value_; }

const Node::Metadata& Node::getMetadata() const noexcept { return metadata_; }

std::string_view to_string(Node::ConfidenceType type) noexcept {
    return type == Node::ConfidenceType::DEDUCTIVE ? "deductive" : "probabilistic";
}

}  // namespace il_message
