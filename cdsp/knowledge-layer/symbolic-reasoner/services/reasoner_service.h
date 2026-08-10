#ifndef REASONER_SERVICE_H
#define REASONER_SERVICE_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "data_types.h"
#include "reasoner_profile.h"

class ReasonerService {
   public:
    // The client-owned queue that receives normalized reasoning-result messages.
    using ReasoningResultMessageQueue = std::vector<nlohmann::json>;
    // Delivers a result to an owner that can schedule its queue update safely.
    using ReasoningResultHandler = std::function<void(nlohmann::json)>;

    explicit ReasonerService(ReasonerProfile reasoner_profile)
        : reasoner_profile_(std::move(reasoner_profile)) {}

    virtual ~ReasonerService() = default;

    virtual bool initialize() = 0;

    virtual bool updateData(const std::string& data,
                            const ReasonerSyntaxType& content_type) = 0;

    virtual bool updateRules(const std::vector<std::string>& rule_file_paths) = 0;

    virtual bool updateOntology(const std::vector<std::string>& ontology_file_paths) = 0;

    // The caller retains ownership of the queue for the service lifetime.
    void setReasoningResultMessageQueue(ReasoningResultMessageQueue& result_message_queue) {
        result_message_queue_ = &result_message_queue;
    }

    // Prefer this for asynchronous engines: the owner receives each result and
    // decides which execution context appends it to the outbound queue.
    void setReasoningResultHandler(ReasoningResultHandler result_handler) {
        result_handler_ = std::move(result_handler);
    }

    const ReasonerProfile& getReasonerProfile() const { return reasoner_profile_; }

   protected:
    // Returns false until either a result handler or a fallback result queue is assigned.
    bool publishReasoningResult(const nlohmann::json& result) {
        if (result_handler_) {
            result_handler_(result);
            return true;
        }

        if (result_message_queue_ == nullptr) {
            return false;
        }

        result_message_queue_->push_back(result);
        return true;
    }

    bool hasReasoningResultMessageQueue() const { return result_message_queue_ != nullptr; }

    ReasonerProfile reasoner_profile_;
    ReasoningResultMessageQueue* result_message_queue_ = nullptr;
    ReasoningResultHandler result_handler_;
};

#endif  // REASONER_SERVICE_H
