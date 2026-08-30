#ifndef IL_REQUEST_REQUEST_H
#define IL_REQUEST_REQUEST_H

#include <string>
#include <string_view>

namespace il_request {

enum class ResponseFormat { NESTED, FLAT };
enum class ResponseRoot { ABSOLUTE, RELATIVE };

struct ResponseOptions {
    ResponseFormat format;
    ResponseRoot root;
};

[[nodiscard]] std::string_view to_string(ResponseFormat format) noexcept;
[[nodiscard]] std::string_view to_string(ResponseRoot root) noexcept;

// Common, validated JSON-RPC context for an outbound IL request.
class Request {
   public:
    virtual ~Request() = default;

    [[nodiscard]] const std::string& getInstance() const noexcept;
    [[nodiscard]] const std::string& getSchema() const noexcept;
    [[nodiscard]] const std::string& getRequestId() const noexcept;
    [[nodiscard]] virtual std::string_view getMethod() const noexcept = 0;

   protected:
    Request(std::string instance, std::string request_id, std::string schema);

   private:
    std::string instance_;
    std::string request_id_;
    std::string schema_;
};

}  // namespace il_request

#endif  // IL_REQUEST_REQUEST_H
