#ifndef IL_REQUEST_SET_REQUEST_H
#define IL_REQUEST_SET_REQUEST_H

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "il_request/node.h"
#include "il_request/request.h"

namespace il_request {

enum class SyncMode { OFF, LOCAL, REMOTE };

struct SetOptions {
    std::optional<std::string> path;
    std::optional<SyncMode> sync;
    std::optional<bool> timeseries;
};

[[nodiscard]] std::string_view to_string(SyncMode sync_mode) noexcept;

class SetRequest final : public Request {
   public:
    SetRequest(std::string instance,
               Node node,
               SetOptions options = {},
               std::string schema = "Vehicle");
    SetRequest(std::string instance,
               std::vector<Node> nodes,
               std::string batch_request_id,
               SetOptions options = {},
               std::string schema = "Vehicle");

    [[nodiscard]] const std::vector<Node>& getNodes() const noexcept;
    [[nodiscard]] const SetOptions& getOptions() const noexcept;
    [[nodiscard]] std::string_view getMethod() const noexcept override;
    friend std::ostream& operator<<(std::ostream& os, const SetRequest& request);

   private:
    std::vector<Node> nodes_;
    SetOptions options_;
};

nlohmann::json to_json(const SetRequest& request);

}  // namespace il_request

#endif  // IL_REQUEST_SET_REQUEST_H
