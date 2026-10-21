#ifndef CQELS_REASONER_SERVICE_H
#define CQELS_REASONER_SERVICE_H

#include <string>
#include <vector>

#include "reasoner_service_push.h"

// CQELS will use the push workflow once its engine integration is implemented.
class CQELSReasonerService final : public ReasonerServicePush {
   public:
    using ReasonerServicePush::ReasonerServicePush;

   protected:
    // This placeholder accepts a profile but intentionally performs no CQELS work yet.
    bool registerConfiguredQueries() override { return true; }

    bool submitDataToReasoner(const std::string&,
                              const ReasonerSyntaxType&) override {
        return false;
    }

    bool updateRulesInReasoner(const std::vector<std::string>&) override { return false; }

    bool updateOntologyInReasoner(const std::vector<std::string>&) override { return false; }
};

#endif  // CQELS_REASONER_SERVICE_H
