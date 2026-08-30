#ifndef IL_MESSAGE_UNSUBSCRIBE_REQUEST_H
#define IL_MESSAGE_UNSUBSCRIBE_REQUEST_H

#include <optional>
#include <ostream>

#include <nlohmann/json.hpp>

#include "il_request/node.h"
#include "il_request/request.h"

namespace il_request {

class UnsubscribeRequest final : public Request {
   public:
    UnsubscribeRequest(std::string instance, Node node, std::string schema = "Vehicle");
    UnsubscribeRequest(std::string instance,
                       std::string root_request_id,
                       std::string schema = "Vehicle");

    [[nodiscard]] const std::optional<Node>& getNode() const noexcept;
    [[nodiscard]] std::string_view getMethod() const noexcept override;
    friend std::ostream& operator<<(std::ostream& os, const UnsubscribeRequest& request);

   private:
    std::optional<Node> node_;
};

nlohmann::json to_json(const UnsubscribeRequest& request);

}  // namespace il_request

#endif  // IL_MESSAGE_UNSUBSCRIBE_REQUEST_H
