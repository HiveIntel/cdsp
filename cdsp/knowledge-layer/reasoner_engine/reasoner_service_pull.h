#ifndef REASONER_SERVICE_PULL_H
#define REASONER_SERVICE_PULL_H

#include <string>
#include <utility>
#include <vector>

#include "reasoner_service.h"

// Common pull workflow: ingest an update, query the engine, then publish each result.
class ReasonerServicePull : public ReasonerService {
   public:
    using ReasonerService::ReasonerService;

    ~ReasonerServicePull() override = default;

    bool initialize() final {
        if (initialized_) {
            return true;
        }

        initialized_ = initializeReasoner();
        return initialized_;
    }

    bool updateData(const std::string& data, const ReasonerSyntaxType& content_type) final {
        if (!initialize() || !updateDataInReasoner(data, content_type)) {
            return false;
        }

        for (const auto& result : queryReasoner()) {
            if (!result.is_null() && !publishReasoningResult(result)) {
                return false;
            }
        }

        return true;
    }

    bool updateRules(const std::vector<std::string>& rule_file_paths) final {
        return initialize() && updateRulesInReasoner(rule_file_paths);
    }

    bool updateOntology(const std::vector<std::string>& ontology_file_paths) final {
        return initialize() && updateOntologyInReasoner(ontology_file_paths);
    }

   protected:
    // Engine implementations provide the operations invoked by the common workflow above.
    virtual bool initializeReasoner() = 0;
    virtual bool updateDataInReasoner(const std::string& data,
                                      const ReasonerSyntaxType& content_type) = 0;
    virtual std::vector<nlohmann::json> queryReasoner() = 0;
    virtual bool updateRulesInReasoner(const std::vector<std::string>& rule_file_paths) = 0;
    virtual bool updateOntologyInReasoner(
        const std::vector<std::string>& ontology_file_paths) = 0;

   private:
    bool initialized_ = false;
};

#endif  // REASONER_SERVICE_PULL_H
