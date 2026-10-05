#include "il_message_parser.h"
#include "il_request_get_dto.h"
#include "il_request_set_dto.h"
#include "il_request_subscribe_dto.h"
#include "il_request_unsubscribe_dto.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

using nlohmann::json;

TEST(ILRequestSerializationTest, GetIncludesConfiguredResponseShape) {
    const il_request::GetRequestDTO request{
        "Vehicle", "VIN_1805", "Speed", "flat", "absolute"};

    const json message = request;

    EXPECT_EQ(message.at("jsonrpc"), "2.0");
    EXPECT_EQ(message.at("method"), "get");
    EXPECT_EQ(message.at("id"), "get-VIN_1805-Vehicle_Speed");
    EXPECT_EQ(message.at("params"),
              (json{{"schema", "Vehicle"},
                    {"instance", "VIN_1805"},
                    {"path", "Speed"},
                    {"format", "flat"},
                    {"root", "absolute"}}));
}

TEST(ILRequestSerializationTest, GetOmitsEveryAbsentOptionalParameter) {
    const il_request::GetRequestDTO request{
        "Vehicle", "VIN_1805", std::nullopt, std::nullopt, std::nullopt};

    const json message = request;

    EXPECT_EQ(message.at("id"), "get-VIN_1805-Vehicle_");
    EXPECT_EQ(message.at("params"),
              (json{{"schema", "Vehicle"}, {"instance", "VIN_1805"}}));
}

TEST(ILRequestSerializationTest, SetSerializesScalarDataAndGeneratedTimestamp) {
    const auto generated = std::chrono::system_clock::time_point{
        std::chrono::seconds{42} + std::chrono::nanoseconds{123}};
    const il_request::SetRequestDTO request{
        "Vehicle",
        "VIN_1805",
        il_data_point::DataPointDTO{"Speed", std::int64_t{120}},
        generated};

    const json message = request;

    EXPECT_EQ(message.at("method"), "set");
    EXPECT_EQ(message.at("id"), "set-VIN_1805-Vehicle_Speed");
    EXPECT_EQ(message.at("params").at("data"), 120);
    EXPECT_EQ(message.at("params")
                  .at("metadata")
                  .at("")
                  .at("timestamps")
                  .at("generated")
                  .at("seconds"),
              42);
    EXPECT_EQ(message.at("params")
                  .at("metadata")
                  .at("")
                  .at("timestamps")
                  .at("generated")
                  .at("nanos"),
              123);
}

TEST(ILRequestSerializationTest, SetPreservesEachSupportedScalarType) {
    const il_request::SetRequestDTO string_request{
        "Vehicle", "VIN_1805", {"Mode", std::string{"sport"}}, std::nullopt};
    const il_request::SetRequestDTO double_request{
        "Vehicle", "VIN_1805", {"Speed", 42.5}, std::nullopt};
    const il_request::SetRequestDTO bool_request{
        "Vehicle", "VIN_1805", {"IsMoving", true}, std::nullopt};

    EXPECT_EQ(json(string_request).at("params").at("data"), "sport");
    EXPECT_EQ(json(double_request).at("params").at("data"), 42.5);
    EXPECT_EQ(json(bool_request).at("params").at("data"), true);
}

TEST(ILRequestSerializationTest, SubscribeAndUnsubscribeUseMatchingScope) {
    const il_request::SubscribeRequestDTO subscribe{
        "Vehicle", "VIN_1805", "CurrentLocation", "nested", "relative"};
    const il_request::UnsubscribeRequestDTO unsubscribe{
        "Vehicle", "VIN_1805", "CurrentLocation"};

    const json subscribe_message = subscribe;
    const json unsubscribe_message = unsubscribe;

    EXPECT_EQ(subscribe_message.at("id"),
              "subscribe-VIN_1805-Vehicle_CurrentLocation");
    EXPECT_EQ(unsubscribe_message.at("id"),
              "unsubscribe-VIN_1805-Vehicle_CurrentLocation");
    EXPECT_EQ(subscribe_message.at("params").at("path"),
              unsubscribe_message.at("params").at("path"));
    EXPECT_EQ(subscribe_message.at("params").at("instance"),
              unsubscribe_message.at("params").at("instance"));
    EXPECT_EQ(subscribe_message.at("params").at("schema"),
              unsubscribe_message.at("params").at("schema"));
    EXPECT_FALSE(unsubscribe_message.at("params").contains("format"));
    EXPECT_FALSE(unsubscribe_message.at("params").contains("root"));
}

TEST(ILMessageParserTest, ParsesDataAndPreservesMetadata) {
    const auto parsed = il_message::ILMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":"subscribe-1","result":{"instance":"VIN_1805","data":{"Vehicle.Speed":42.5},"metadata":{"Vehicle.Speed":{"timestamps":{"generated":{"seconds":42,"nanos":123},"received":{"seconds":43,"nanos":456}}}}}})");

    ASSERT_TRUE(std::holds_alternative<il_message::DataMessageDTO>(parsed));
    const auto& data = std::get<il_message::DataMessageDTO>(parsed);
    EXPECT_EQ(data.id, "subscribe-1");
    EXPECT_EQ(data.instance, "VIN_1805");
    ASSERT_EQ(data.data.size(), 1U);
    EXPECT_EQ(data.data.front().path, "Vehicle.Speed");
    ASSERT_TRUE(std::holds_alternative<double>(data.data.front().value));
    EXPECT_DOUBLE_EQ(std::get<double>(data.data.front().value), 42.5);
    ASSERT_TRUE(data.metadata.has_value());
    ASSERT_EQ(data.metadata->size(), 1U);
    EXPECT_EQ(data.metadata->front().path, "Vehicle.Speed");
    ASSERT_TRUE(data.metadata->front().timestamp.generated.has_value());
    ASSERT_TRUE(data.metadata->front().timestamp.received.has_value());
    EXPECT_EQ(data.metadata->front().timestamp.generated->time_since_epoch(),
              std::chrono::seconds{42} + std::chrono::nanoseconds{123});
    EXPECT_EQ(data.metadata->front().timestamp.received->time_since_epoch(),
              std::chrono::seconds{43} + std::chrono::nanoseconds{456});
}

TEST(ILMessageParserTest, FlattensNestedDataAndPreservesScalarTypes) {
    const auto parsed = il_message::ILMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":"subscribe-2","result":{"instance":"VIN_1805","data":{"Vehicle":{"Available":true,"Axles":2,"Name":"demo","Speed":42.5}}}})");

    ASSERT_TRUE(std::holds_alternative<il_message::DataMessageDTO>(parsed));
    const auto& message = std::get<il_message::DataMessageDTO>(parsed);
    ASSERT_EQ(message.data.size(), 4U);
    EXPECT_FALSE(message.metadata.has_value());

    EXPECT_EQ(message.data[0].path, "Vehicle.Available");
    EXPECT_EQ(std::get<bool>(message.data[0].value), true);
    EXPECT_EQ(message.data[1].path, "Vehicle.Axles");
    EXPECT_EQ(std::get<std::int64_t>(message.data[1].value), 2);
    EXPECT_EQ(message.data[2].path, "Vehicle.Name");
    EXPECT_EQ(std::get<std::string>(message.data[2].value), "demo");
    EXPECT_EQ(message.data[3].path, "Vehicle.Speed");
    EXPECT_DOUBLE_EQ(std::get<double>(message.data[3].value), 42.5);
}

TEST(ILMessageParserTest, ParsesSuccessAndErrorResponses) {
    const auto success = il_message::ILMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":"subscribe-1","result":{}})");
    ASSERT_TRUE(std::holds_alternative<il_message::StatusMessageDTO>(success));
    EXPECT_EQ(std::get<il_message::StatusMessageDTO>(success).id,
              "subscribe-1");

    const auto failure = il_message::ILMessageParser::parseWire(
        R"({"jsonrpc":"2.0","id":"subscribe-1","error":{"code":-32602,"message":"Invalid params","data":{"reason":"path"}}})");
    ASSERT_TRUE(std::holds_alternative<il_message::ErrorMessageDTO>(failure));
    const auto& error = std::get<il_message::ErrorMessageDTO>(failure);
    EXPECT_EQ(error.error.code, -32602);
    EXPECT_EQ(error.error.message, "Invalid params");
    ASSERT_TRUE(error.error.data.has_value());
    EXPECT_EQ(error.error.data->at("reason"), "path");
}

TEST(ILMessageParserTest, RejectsMalformedEnvelopes) {
    EXPECT_THROW(il_message::ILMessageParser::parse(json::array()),
                 std::invalid_argument);
    EXPECT_THROW(il_message::ILMessageParser::parse(
                     {{"jsonrpc", "1.0"},
                      {"id", "x"},
                      {"result", json::object()}}),
                 std::invalid_argument);
    EXPECT_THROW(il_message::ILMessageParser::parse(
                     {{"jsonrpc", "2.0"},
                      {"id", 1},
                      {"result", json::object()}}),
                 std::invalid_argument);
    EXPECT_THROW(il_message::ILMessageParser::parse(
                     {{"jsonrpc", "2.0"},
                      {"id", "x"},
                      {"result", json::object()},
                      {"error", json::object()}}),
                 std::invalid_argument);
    EXPECT_THROW(il_message::ILMessageParser::parseWire("not-json"),
                 json::parse_error);
    EXPECT_THROW(il_message::ILMessageParser::parseWire(
                     R"({"jsonrpc":"2.0","id":"x","result":{"instance":"VIN","data":42}})"),
                 std::invalid_argument);
    EXPECT_THROW(il_message::ILMessageParser::parseWire(
                     R"({"jsonrpc":"2.0","id":"x","result":{"instance":"VIN","data":{"Vehicle.Speed":[]}}})"),
                 std::invalid_argument);
    EXPECT_THROW(il_message::ILMessageParser::parseWire(
                     R"({"jsonrpc":"2.0","id":"x","result":{"instance":"VIN","data":{"Vehicle.Speed":1},"metadata":{"Vehicle.Speed":{"timestamps":{"generated":{"seconds":42,"nanos":1000000000}}}}}})"),
                 std::invalid_argument);
}

TEST(ILMessageDTOTest, StreamOutputIsHumanReadable) {
    const il_message::DataMessageDTO message{
        il_message::ILMessageDTO{"subscription", "2.0"},
        "VIN_1805",
        std::vector<il_data_point::DataPointDTO>{
            {"Vehicle.Speed", std::int64_t{42}}},
        std::nullopt};
    std::ostringstream output;

    output << message;

    EXPECT_NE(output.str().find("DataMessageDTO"), std::string::npos);
    EXPECT_NE(output.str().find("VIN_1805"), std::string::npos);
    EXPECT_NE(output.str().find("Vehicle.Speed"), std::string::npos);
}

}  // namespace
