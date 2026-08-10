#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cqels_reasoner_service.h"
#include "rdf4j_reasoner_service.h"
#include "reasoner_factory.h"
#include "reasoner_profile.h"
#include "reasoner_service_pull.h"
#include "reasoner_service_push.h"

namespace {

std::filesystem::path profilePath(const std::string& engine) {
    return std::filesystem::path(PROJECT_ROOT) / "symbolic-reasoner/examples/driving_style" /
           ("solution_" + engine) / ("driving_style_" + engine + ".json");
}

ReasonerProfile testProfile() {
    return ReasonerProfile{"test", {}, {}, {}};
}

class PullTestService final : public ReasonerServicePull {
   public:
    PullTestService() : ReasonerServicePull(testProfile()) {}

   protected:
    bool initializeReasoner() override { return true; }
    bool updateDataInReasoner(const std::string&, const ReasonerSyntaxType&) override { return true; }
    std::vector<nlohmann::json> queryReasoner() override {
        return {nlohmann::json{{"engine", "pull"}}};
    }
    bool updateRulesInReasoner(const std::vector<std::string>&) override { return true; }
    bool updateOntologyInReasoner(const std::vector<std::string>&) override { return true; }
};

class PushTestService final : public ReasonerServicePush {
   public:
    PushTestService() : ReasonerServicePush(testProfile()) {}

    bool deliver(const nlohmann::json& result) { return publishPushedReasoningResult(result); }

   protected:
    bool registerConfiguredQueries() override { return true; }
    bool submitDataToReasoner(const std::string&, const ReasonerSyntaxType&) override { return true; }
    bool updateRulesInReasoner(const std::vector<std::string>&) override { return true; }
    bool updateOntologyInReasoner(const std::vector<std::string>&) override { return true; }
};

}  // namespace

TEST(ReasonerProfileParserTest, ResolvesRdf4jProfileResources) {
    const auto profile_file = profilePath("rdf4j");
    const ReasonerProfile profile = ReasonerProfileParser::parse(profile_file);

    EXPECT_EQ(profile.name, "RDF4J");
    ASSERT_EQ(profile.ontologies.size(), 1U);
    ASSERT_EQ(profile.rules.size(), 1U);
    ASSERT_EQ(profile.queries.size(), 1U);
    EXPECT_EQ(profile.ontologies.front(), profile_file.parent_path() / "driving_style_ontology.ttl");
    EXPECT_EQ(profile.rules.front(), profile_file.parent_path() / "driving_style_rules.ttl");
    EXPECT_EQ(profile.queries.front(), profile_file.parent_path() / "driving_style_query.rq");
}

TEST(ReasonerFactoryTest, CreatesRdf4jPullServiceFromProfileName) {
    const auto service = ReasonerFactory::initReasoner(profilePath("rdf4j"));

    EXPECT_NE(std::dynamic_pointer_cast<RDF4JReasonerService>(service), nullptr);
    EXPECT_EQ(service->getReasonerProfile().name, "RDF4J");
}

TEST(ReasonerFactoryTest, CreatesCqelsPushServiceFromProfileName) {
    const auto service = ReasonerFactory::initReasoner(profilePath("cqels"));

    EXPECT_NE(std::dynamic_pointer_cast<CQELSReasonerService>(service), nullptr);
    EXPECT_EQ(service->getReasonerProfile().name, "CQELS");
    ASSERT_EQ(service->getReasonerProfile().queries.size(), 1U);
}

TEST(ReasonerServicePullTest, PublishesEachQueryResultToTheConfiguredQueue) {
    PullTestService service;
    ReasonerService::ReasoningResultMessageQueue result_queue;
    service.setReasoningResultMessageQueue(result_queue);

    EXPECT_TRUE(service.updateData("@prefix ex: <http://example.org/> .", ReasonerSyntaxType::TURTLE));
    ASSERT_EQ(result_queue.size(), 1U);
    EXPECT_EQ(result_queue.front(), nlohmann::json({{"engine", "pull"}}));
}

TEST(ReasonerServicePushTest, PublishesCallbackResultsToTheConfiguredQueue) {
    PushTestService service;
    ReasonerService::ReasoningResultMessageQueue result_queue;
    service.setReasoningResultMessageQueue(result_queue);

    ASSERT_TRUE(service.initialize());
    EXPECT_TRUE(service.deliver(nlohmann::json{{"engine", "push"}}));
    ASSERT_EQ(result_queue.size(), 1U);
    EXPECT_EQ(result_queue.front(), nlohmann::json({{"engine", "push"}}));
}

TEST(ReasonerServicePushTest, DeliversResultsThroughTheRegisteredHandler) {
    PushTestService service;
    ReasonerService::ReasoningResultMessageQueue result_queue;
    service.setReasoningResultMessageQueue(result_queue);

    nlohmann::json delivered_result;
    service.setReasoningResultHandler([&delivered_result](nlohmann::json result) {
        delivered_result = std::move(result);
    });

    EXPECT_TRUE(service.deliver(nlohmann::json{{"engine", "push"}}));
    EXPECT_EQ(delivered_result, nlohmann::json({{"engine", "push"}}));
    EXPECT_TRUE(result_queue.empty());
}
