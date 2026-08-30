#ifndef IL_MESSAGE_SUBSCRIBE_REQUEST_H
#define IL_MESSAGE_SUBSCRIBE_REQUEST_H

#include <optional>
#include <ostream>

#include <nlohmann/json.hpp>

#include "il_request/node.h"
#include "il_request/request.h"

namespace il_request {

class SubscribeRequest final : public Request {
   public:
    SubscribeRequest(std::string instance,
                     Node node,
                     ResponseOptions options = {ResponseFormat::NESTED,
                                                ResponseRoot::ABSOLUTE},
                     std::string schema = "Vehicle");
    SubscribeRequest(std::string instance,
                     std::string root_request_id,
                     ResponseOptions options = {ResponseFormat::NESTED,
                                                ResponseRoot::ABSOLUTE},
                     std::string schema = "Vehicle");

    [[nodiscard]] const std::optional<Node>& getNode() const noexcept;
    [[nodiscard]] ResponseOptions getResponseOptions() const noexcept;
    [[nodiscard]] std::string_view getMethod() const noexcept override;
    friend std::ostream& operator<<(std::ostream& os, const SubscribeRequest& request);

   private:
    std::optional<Node> node_;
    ResponseOptions options_;
};

nlohmann::json to_json(const SubscribeRequest& request);

}  // namespace il_request

#endif  // IL_MESSAGE_SUBSCRIBE_REQUEST_H
