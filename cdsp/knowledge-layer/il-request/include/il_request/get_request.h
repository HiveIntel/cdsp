#ifndef IL_REQUEST_GET_REQUEST_H
#define IL_REQUEST_GET_REQUEST_H

#include <optional>
#include <ostream>

#include <nlohmann/json.hpp>

#include "il_request/node.h"
#include "il_request/request.h"

namespace il_request {

class GetRequest final : public Request {
   public:
    GetRequest(std::string instance,
               Node node,
               ResponseOptions options = {ResponseFormat::NESTED, ResponseRoot::RELATIVE},
               std::string schema = "Vehicle");
    GetRequest(std::string instance,
               std::string root_request_id,
               ResponseOptions options = {ResponseFormat::NESTED, ResponseRoot::RELATIVE},
               std::string schema = "Vehicle");

    [[nodiscard]] const std::optional<Node>& getNode() const noexcept;
    [[nodiscard]] ResponseOptions getResponseOptions() const noexcept;
    [[nodiscard]] std::string_view getMethod() const noexcept override;
    friend std::ostream& operator<<(std::ostream& os, const GetRequest& request);

   private:
    std::optional<Node> node_;
    ResponseOptions options_;
};

nlohmann::json to_json(const GetRequest& request);

}  // namespace il_request

#endif  // IL_REQUEST_GET_REQUEST_H
