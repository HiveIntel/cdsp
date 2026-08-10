#ifndef MOCK_REASONER_SERVICE_H
#define MOCK_REASONER_SERVICE_H

#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "reasoner_service_pull.h"

class MockReasonerService : public ReasonerServicePull {
   public:
    explicit MockReasonerService(ReasonerProfile profile)
        : ReasonerServicePull(std::move(profile)) {}

   protected:
    MOCK_METHOD(bool, initializeReasoner, (), (override));
    MOCK_METHOD(bool, updateDataInReasoner,
                (const std::string& data, const ReasonerSyntaxType& content_type), (override));
    MOCK_METHOD(std::vector<nlohmann::json>, queryReasoner, (), (override));
    MOCK_METHOD(bool, updateRulesInReasoner, (const std::vector<std::string>& rule_file_paths),
                (override));
    MOCK_METHOD(bool, updateOntologyInReasoner,
                (const std::vector<std::string>& ontology_file_paths), (override));
};

#endif  // MOCK_REASONER_SERVICE_H
