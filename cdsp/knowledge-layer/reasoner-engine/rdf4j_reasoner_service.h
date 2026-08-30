#ifndef RDF4J_REASONER_SERVICE_H
#define RDF4J_REASONER_SERVICE_H

#include <string>
#include <vector>

#include "reasoner_service_pull.h"

// Pull-based adapter for the standalone RDF4J HTTP service.
class RDF4JReasonerService final : public ReasonerServicePull {
   public:
    using ReasonerServicePull::ReasonerServicePull;

   protected:
    bool initializeReasoner() override;
    bool updateDataInReasoner(const std::string& data,
                              const ReasonerSyntaxType& content_type) override;
    std::vector<nlohmann::json> queryReasoner() override;
    bool updateRulesInReasoner(const std::vector<std::string>& rule_file_paths) override;
    bool updateOntologyInReasoner(const std::vector<std::string>& ontology_file_paths) override;

   private:
    std::string request(const std::string& method,
                        const std::string& target,
                        const std::string& content_type,
                        const std::string& accept,
                        const std::string& body) const;
    std::string datastoreTarget(const std::string& suffix = "") const;
};

#endif  // RDF4J_REASONER_SERVICE_H
