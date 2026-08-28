#ifndef RDF4J_REASONER_SERVICE_H
#define RDF4J_REASONER_SERVICE_H

#include <string>
#include <vector>

#include "reasoner_service_pull.h"

// RDF4J will use the pull workflow once its engine integration is implemented.
class RDF4JReasonerService final : public ReasonerServicePull {
   public:
    using ReasonerServicePull::ReasonerServicePull;

   protected:
    // This placeholder accepts a profile but intentionally performs no RDF4J work yet.
    bool initializeReasoner() override { return true; }

    bool updateDataInReasoner(const std::string&,
                              const ReasonerSyntaxType&) override {
        return false;
    }

    std::vector<nlohmann::json> queryReasoner() override { return {}; }

    bool updateRulesInReasoner(const std::vector<std::string>&) override { return false; }

    bool updateOntologyInReasoner(const std::vector<std::string>&) override { return false; }
};

#endif  // RDF4J_REASONER_SERVICE_H
