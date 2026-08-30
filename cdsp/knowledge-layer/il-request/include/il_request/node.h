#ifndef IL_REQUEST_NODE_H
#define IL_REQUEST_NODE_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace il_request {

// Node is the complete per-signal model, including the optional metadata sent
// with a SET request. Metadata has no independent lifecycle or public class.
class Node {
   public:
    enum class ConfidenceType : std::uint8_t { DEDUCTIVE, PROBABILISTIC };

    struct Metadata {
        struct Timestamps {
            std::optional<std::chrono::system_clock::time_point> received;
            std::optional<std::chrono::system_clock::time_point> generated;
        } timestamps;

        struct Origin {
            std::optional<std::string> name;
            std::optional<std::string> uri;
        };

        struct Confidence {
            ConfidenceType type;
            std::string value;
        };

        std::optional<Origin> origin;
        std::optional<Confidence> confidence;
    };

    explicit Node(std::string name,
                  std::optional<std::string> value = std::nullopt,
                  Metadata metadata = {});

    [[nodiscard]] const std::string& getName() const noexcept;
    [[nodiscard]] const std::optional<std::string>& getValue() const noexcept;
    [[nodiscard]] const Metadata& getMetadata() const noexcept;

   private:
    std::string name_;
    std::optional<std::string> value_;
    Metadata metadata_;
};

[[nodiscard]] std::string_view to_string(Node::ConfidenceType type) noexcept;

}  // namespace il_request

#endif  // IL_REQUEST_NODE_H
