#include "reasoner_factory.h"

#include <memory>
#include <stdexcept>
#include <utility>

#include "cqels_reasoner_service.h"
#include "rdf4j_reasoner_service.h"
#include "reasoner_profile.h"

std::shared_ptr<ReasonerService> ReasonerFactory::initReasoner(
    const std::filesystem::path& reasoner_profile_path) {
    ReasonerProfile profile = ReasonerProfileParser::parse(reasoner_profile_path);
    std::shared_ptr<ReasonerService> reasoner_service;

    if (profile.name == "RDF4J") {
        reasoner_service = std::make_shared<RDF4JReasonerService>(std::move(profile));
    } else if (profile.name == "CQELS") {
        reasoner_service = std::make_shared<CQELSReasonerService>(std::move(profile));
    } else {
        throw std::invalid_argument("Unsupported reasoner profile name: " + profile.name);
    }

    if (!reasoner_service->initialize()) {
        throw std::runtime_error("Failed to initialize reasoner service for profile: " +
                                 reasoner_profile_path.string());
    }

    return reasoner_service;
}
