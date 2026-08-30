#ifndef REASONER_SERVICE_PUSH_H
#define REASONER_SERVICE_PUSH_H

#include <string>
#include <utility>
#include <vector>

#include "reasoner_service.h"

// Common push workflow: register profile queries, submit updates, and publish callbacks.
class ReasonerServicePush : public ReasonerService {
   public:
    using ReasonerService::ReasonerService;

    ~ReasonerServicePush() override = default;

    bool initialize() final {
        if (initialized_) {
            return true;
        }

        initialized_ = registerConfiguredQueries();
        return initialized_;
    }

    bool updateData(const std::string& data, const ReasonerSyntaxType& content_type) final {
        return initialize() && submitDataToReasoner(data, content_type);
    }

    bool updateRules(const std::vector<std::string>& rule_file_paths) final {
        return initialize() && updateRulesInReasoner(rule_file_paths);
    }

    bool updateOntology(const std::vector<std::string>& ontology_file_paths) final {
        return initialize() && updateOntologyInReasoner(ontology_file_paths);
    }

   protected:
    // Engine callbacks use this helper to forward a normalized result to the client queue.
    bool publishPushedReasoningResult(const nlohmann::json& result) {
        return publishReasoningResult(result);
    }

    virtual bool registerConfiguredQueries() = 0;
    virtual bool submitDataToReasoner(const std::string& data,
                                      const ReasonerSyntaxType& content_type) = 0;
    virtual bool updateRulesInReasoner(const std::vector<std::string>& rule_file_paths) = 0;
    
    virtual bool updateOntologyInReasoner(const std::vector<std::string>& ontology_file_paths) = 0;

   private:
    bool initialized_ = false;
};

#endif  // REASONER_SERVICE_PUSH_H
