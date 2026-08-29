#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "il_message/get_request.h"
#include "il_message/inbound_message_parser.h"
#include "il_message/set_request.h"
#include "il_message/subscribe_request.h"
#include "il_message/unsubscribe_request.h"

using il_message::DataMessageDto;
using il_message::GetRequest;
using il_message::InboundMessageParser;
using il_message::Node;
using il_message::Request;
using il_message::ResponseFormat;
using il_message::ResponseOptions;
using il_message::ResponseRoot;
using il_message::SetOptions;
using il_message::SetRequest;
using il_message::StatusMessageDto;
using il_message::SubscribeRequest;
using il_message::SyncMode;
using il_message::UnsubscribeRequest;

static_assert(std::is_abstract_v<Request>);

TEST(IlRequestSerializationTest, GetUsesTheNodeNameAsTheRequestId) {
    const GetRequest request("VIN-123", Node{"Speed"});
    const auto message = il_message::to_json(request);

    EXPECT_EQ(message["id"], "Speed");
    EXPECT_EQ(message["params"]["path"], "Speed");
    EXPECT_EQ(message["params"]["format"], "nested");
    EXPECT_EQ(message["params"]["root"], "relative");
}

TEST(IlRequestSerializationTest, GetSerializesEveryResponseFormatAndRootCombination) {
    struct ExpectedOptions {
        ResponseOptions options;
        const char* format;
        const char* root;
    };
    constexpr ExpectedOptions kOptions[] = {
        {{ResponseFormat::FLAT, ResponseRoot::RELATIVE}, "flat", "relative"},
        {{ResponseFormat::FLAT, ResponseRoot::ABSOLUTE}, "flat", "absolute"},
        {{ResponseFormat::NESTED, ResponseRoot::RELATIVE}, "nested", "relative"},
        {{ResponseFormat::NESTED, ResponseRoot::ABSOLUTE}, "nested", "absolute"},
    };

    for (const auto& expected : kOptions) {
        const GetRequest request("VIN-123", Node{"CurrentLocation"}, expected.options);
        const auto message = il_message::to_json(request);

        EXPECT_EQ(message["id"], "CurrentLocation");
        EXPECT_EQ(message["params"]["path"], "CurrentLocation");
        EXPECT_EQ(message["params"]["format"], expected.format);
        EXPECT_EQ(message["params"]["root"], expected.root);
    }
}

TEST(IlRequestSerializationTest, SubscribeUsesOnePathPerRequestAndCanOverrideItsDefaults) {
    const SubscribeRequest default_request("VIN-123", Node{"Speed"});
    const auto default_message = il_message::to_json(default_request);
    EXPECT_EQ(default_message["id"], "Speed");
    EXPECT_EQ(default_message["params"]["format"], "nested");
    EXPECT_EQ(default_message["params"]["root"], "absolute");

    const SubscribeRequest rdf_mapper_request(
        "VIN-123", Node{"Speed"}, {ResponseFormat::FLAT, ResponseRoot::RELATIVE});
    const auto rdf_mapper_message = il_message::to_json(rdf_mapper_request);
    EXPECT_EQ(rdf_mapper_message["id"], "Speed");
    EXPECT_EQ(rdf_mapper_message["params"]["format"], "flat");
    EXPECT_EQ(rdf_mapper_message["params"]["root"], "relative");
}

TEST(IlRequestSerializationTest, RootRequestsRequireAnExplicitRequestId) {
    const GetRequest request("VIN-123", "vehicle-root");
    const auto message = il_message::to_json(request);

    EXPECT_EQ(message["id"], "vehicle-root");
    EXPECT_FALSE(message["params"].contains("path"));
}

TEST(IlRequestSerializationTest, UnsubscribeUsesTheNodeNameAsTheRequestId) {
    const UnsubscribeRequest request("VIN-123", Node{"Speed"});
    const auto message = il_message::to_json(request);

    EXPECT_EQ(message["method"], "unsubscribe");
    EXPECT_EQ(message["id"], "Speed");
    EXPECT_EQ(message["params"]["path"], "Speed");
    EXPECT_FALSE(message["params"].contains("format"));
    EXPECT_FALSE(message["params"].contains("root"));
}

TEST(IlRequestSerializationTest, SetSupportsMetadataAndEveryOptionalSetParameter) {
    Node::Metadata metadata;
    metadata.timestamps.received = std::chrono::system_clock::time_point{std::chrono::seconds{41}};
    metadata.timestamps.generated = std::chrono::system_clock::time_point{std::chrono::seconds{42}};
    metadata.origin = Node::Metadata::Origin{std::string{"SemanticReasoner"},
                                             std::string{"urn:cdsp:reasoner"}};
    metadata.confidence = Node::Metadata::Confidence{Node::ConfidenceType::DEDUCTIVE, "100"};
    const SetOptions options{std::string{"AI.Reasoner"}, SyncMode::REMOTE, true};
    const SetRequest request("VIN-123", Node{"Speed", "120", metadata}, options);
    const auto message = il_message::to_json(request);

    EXPECT_EQ(message["id"], "Speed");
    EXPECT_EQ(message["params"]["data"]["Speed"], 120);
    EXPECT_EQ(message["params"]["path"], "AI.Reasoner");
    EXPECT_EQ(message["params"]["sync"], "remote");
    EXPECT_TRUE(message["params"]["timeseries"]);
    EXPECT_EQ(message["params"]["metadata"]["Speed"]["timestamps"]["received"]["seconds"], 41);
    EXPECT_EQ(message["params"]["metadata"]["Speed"]["timestamps"]["generated"]["seconds"], 42);
    EXPECT_EQ(message["params"]["metadata"]["Speed"]["origin"]["type"]["name"],
              "SemanticReasoner");
    EXPECT_EQ(message["params"]["metadata"]["Speed"]["confidence"]["type"], "deductive");
}

TEST(IlRequestSerializationTest, SetBatchNeedsAnExplicitBatchRequestId) {
    const SetRequest request("VIN-123",
                             std::vector<Node>{Node{"Speed", "120"}, Node{"Powertrain.RPM", "3000"}},
                             "powertrain-update");
    const auto message = il_message::to_json(request);

    EXPECT_EQ(message["id"], "powertrain-update");
    EXPECT_EQ(message["params"]["data"]["Speed"], 120);
    EXPECT_EQ(message["params"]["data"]["Powertrain.RPM"], 3000);
}

TEST(IlRequestSerializationTest, RejectsInvalidRequestContextAndEmptySetBatch) {
    EXPECT_THROW((GetRequest{"", Node{"Speed"}}), std::invalid_argument);
    EXPECT_THROW((GetRequest{"VIN-123", ""}), std::invalid_argument);
    EXPECT_THROW((Node{""}), std::invalid_argument);
    EXPECT_THROW((SetRequest{"VIN-123", std::vector<Node>{}, "empty-batch"}),
                 std::invalid_argument);
}

TEST(InboundMessageParserTest, PreservesEveryDataShapeFromTheInformationLayer) {
    struct DataShape {
        const char* name;
        nlohmann::json data;
    };
    const std::vector<DataShape> shapes = {
        {"flat-relative", {{"", 60}}},
        {"flat-absolute", {{"CurrentLocation.Latitude", 60}}},
        {"nested-relative", 60},
        {"nested-absolute", {{"CurrentLocation", {{"Latitude", 60}}}}},
        {"branch", {{"Latitude", 60}, {"Longitude", 50}}},
    };

    for (const auto& shape : shapes) {
        SCOPED_TRACE(shape.name);
        const nlohmann::json message = {
            {"jsonrpc", "2.0"},
            {"id", "CurrentLocation.Latitude"},
            {"result",
             {{"instance", "VIN-123"},
              {"data", shape.data},
              {"metadata", {{"CurrentLocation.Latitude", {{"timestamps", nlohmann::json::object()}}}}}}},
        };

        const auto parsed = InboundMessageParser::parse(message);
        ASSERT_TRUE(std::holds_alternative<DataMessageDto>(parsed));
        const auto& data_message = std::get<DataMessageDto>(parsed);
        EXPECT_EQ(data_message.instance, "VIN-123");
        EXPECT_EQ(data_message.data, shape.data);
        ASSERT_TRUE(data_message.metadata.has_value());
        EXPECT_TRUE(std::holds_alternative<std::string>(data_message.id));
        EXPECT_EQ(std::get<std::string>(data_message.id), "CurrentLocation.Latitude");
    }
}

TEST(InboundMessageParserTest, ParsesSuccessAndErrorStatusMessages) {
    const auto acknowledgement = InboundMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":"Speed","result":{}})");
    ASSERT_TRUE(std::holds_alternative<StatusMessageDto>(acknowledgement));
    const auto& success = std::get<StatusMessageDto>(acknowledgement);
    EXPECT_TRUE(success.isSuccess());
    EXPECT_EQ(std::get<std::string>(success.id), "Speed");

    const auto failure = InboundMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":null,"error":{"code":-32602,"message":"Invalid params","data":{"reason":"path"}}})");
    ASSERT_TRUE(std::holds_alternative<StatusMessageDto>(failure));
    const auto& error = std::get<StatusMessageDto>(failure);
    ASSERT_FALSE(error.isSuccess());
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(error.id));
    ASSERT_TRUE(error.error->data.has_value());
    EXPECT_EQ(error.error->code, -32602);
    EXPECT_EQ(error.error->data->at("reason"), "path");
}

TEST(InboundMessageParserTest, SupportsIntegerIdsAndRejectsMalformedEnvelopes) {
    const auto parsed = InboundMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":7,"result":{"instance":"VIN-123","data":42}})");
    ASSERT_TRUE(std::holds_alternative<DataMessageDto>(parsed));
    EXPECT_EQ(std::get<std::int64_t>(std::get<DataMessageDto>(parsed).id), 7);

    EXPECT_THROW(InboundMessageParser::parseWire(R"({"jsonrpc":"2.0","id":"x"})"),
                 std::invalid_argument);
    EXPECT_THROW(InboundMessageParser::parseWire(
                     R"({"jsonrpc":"2.0","id":"x","result":{},"error":{}})"),
                 std::invalid_argument);
    EXPECT_THROW(InboundMessageParser::parseWire(
                     R"({"jsonrpc":"2.0","id":true,"result":{}})"),
                 std::invalid_argument);
}
