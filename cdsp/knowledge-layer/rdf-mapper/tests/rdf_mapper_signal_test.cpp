#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "il_message_parser.h"
#include "rdf_mapper.h"

namespace rdf_mapper_test {

// Defined in serd_rdf_writer_test.cpp; the production reader remains unchanged.
std::vector<rdf_mapper::RDFTriple> readSerializedRdf(std::string_view document,
                                                     rdf_mapper::RDFFormat format);

}  // namespace rdf_mapper_test

namespace {

using rdf_mapper::RDFFormat;
using rdf_mapper::RDFGraph;
using rdf_mapper::RDFMapper;
using rdf_mapper::RDFNode;
using rdf_mapper::RDFNodeType;
using rdf_mapper::RDFTriple;
using TimePoint = std::chrono::system_clock::time_point;
using Scalar = decltype(il_data_point::DataPointDTO{}.value);

const std::string kBase = "https://example.org/instances/";
const std::string kRdfType = "http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
const std::string kSosa = "http://www.w3.org/ns/sosa/";
const std::string kVss = "https://covesa.org/vss/latest/model#";
const std::string kXsd = "http://www.w3.org/2001/XMLSchema#";
const std::string kGeneratedText = "1970-01-01T00:00:42.000000123Z";

TimePoint generatedTime() {
    return TimePoint{std::chrono::seconds{42} + std::chrono::nanoseconds{123}};
}

il_data_point::MetadataPointDTO metadata(std::string path, TimePoint generated = generatedTime()) {
    il_data_point::TimestampsDTO timestamps;
    timestamps.generated = generated;
    return {std::move(path), timestamps};
}

il_message::DataMessageDTO speedMessage(Scalar value = 42.5, TimePoint time = generatedTime()) {
    il_message::DataMessageDTO message;
    message.instance = "instance-field-is-not-the-caller-vehicle-id";
    message.data.push_back({"Vehicle.Speed", std::move(value)});
    message.metadata =
        std::vector<il_data_point::MetadataPointDTO>{metadata("Vehicle.Speed", time)};
    return message;
}

RDFNode instance(const std::string& part = "", const std::string& encoded_id = "VIN_test") {
    return RDFNode::iri(kBase + "Vehicle_" + encoded_id + (part.empty() ? "" : "/" + part));
}

RDFNode observation(const std::string& signal_path, const std::string& time = kGeneratedText,
                    const std::string& encoded_id = "VIN_test") {
    return RDFNode::iri(instance("", encoded_id).value + "/observation/" + signal_path + "/" +
                        time);
}

void expectTriples(const std::vector<RDFTriple>& actual, const std::vector<RDFTriple>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (const auto& triple : expected) {
        EXPECT_EQ(std::count(actual.begin(), actual.end(), triple), 1) << triple;
    }
}

std::vector<RDFTriple> observationTriples(const RDFNode& subject, const RDFNode& feature,
                                          const std::string& property, const std::string& value,
                                          const std::string& datatype,
                                          const std::string& time = kGeneratedText) {
    return {
        {subject, RDFNode::iri(kRdfType), RDFNode::iri(kSosa + "Observation")},
        {subject, RDFNode::iri(kSosa + "hasFeatureOfInterest"), feature},
        {subject, RDFNode::iri(kSosa + "observedProperty"), RDFNode::iri(kVss + property)},
        {subject, RDFNode::iri(kSosa + "hasSimpleResult"),
         RDFNode::literal(value, kXsd + datatype)},
        {subject, RDFNode::iri(kSosa + "resultTime"), RDFNode::literal(time, kXsd + "dateTime")}};
}

class RDFMapperSignalTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Read only the user's fixtures. These tests never create or rewrite TTL/JSON.
        ASSERT_TRUE(std::filesystem::is_regular_file(vehicle_shapes_));
        ASSERT_TRUE(std::filesystem::is_regular_file(speed_shapes_));
        ASSERT_TRUE(std::filesystem::is_regular_file(location_shapes_));
    }

    RDFMapper speedMapper() const { return RDFMapper{vehicle_shapes_, speed_shapes_, kBase}; }
    RDFMapper locationMapper() const { return RDFMapper{vehicle_shapes_, location_shapes_, kBase}; }

    il_message::DataMessageDTO wireFixture(const std::string& filename) const {
        const auto path = fixtures_ / "rdf_mapper_signal" / filename;
        std::ifstream input{path, std::ios::binary};
        if (!input) {
            throw std::runtime_error("Cannot open supplied wire fixture: " + path.string());
        }
        const auto parsed = il_message::ILMessageParser::parse(nlohmann::json::parse(input));
        if (const auto* message = std::get_if<il_message::DataMessageDTO>(&parsed)) {
            return *message;
        }
        throw std::runtime_error("Wire fixture is not a data message: " + path.string());
    }

    const std::filesystem::path fixtures_{RDF_MAPPER_TEST_FIXTURE_DIR};
    const std::filesystem::path vehicle_shapes_{fixtures_ / "vehicle_shacl" / "vehicle_kb.ttl"};
    const std::filesystem::path speed_shapes_{fixtures_ / "signal_shacl" / "speed_obs.ttl"};
    const std::filesystem::path location_shapes_{fixtures_ / "signal_shacl" /
                                                 "current_location_obs.ttl"};
};

class RDFMapperSerializationTest : public RDFMapperSignalTest,
                                   public ::testing::WithParamInterface<RDFFormat> {};

TEST_P(RDFMapperSerializationTest, VehicleWrapperPreservesTheTwentyOneTripleGraph) {
    const auto mapper = speedMapper();
    const std::string id = "VIN /%#?\xC3\xA4";
    const auto expected = mapper.vehicleToTriples(id);
    const auto serialized = mapper.vehicleToRdf(id, GetParam());
    const auto parsed = rdf_mapper_test::readSerializedRdf(serialized, GetParam());
    ASSERT_EQ(parsed.size(), 21U);
    expectTriples(parsed, expected);
}

TEST_P(RDFMapperSerializationTest, SpeedWrapperPreservesTheSixTripleGraphAndReplayIdentity) {
    const auto mapper = speedMapper();
    const auto message = wireFixture("speed_message.json");
    const auto serialized = mapper.signalToRdf(message, "VIN_test", GetParam());
    const auto parsed = rdf_mapper_test::readSerializedRdf(serialized, GetParam());
    ASSERT_EQ(parsed.size(), 6U);
    expectTriples(parsed, mapper.signalToTriples(message, "VIN_test"));
    EXPECT_EQ(mapper.signalToRdf(message, "VIN_test", GetParam()), serialized);
}

TEST_P(RDFMapperSerializationTest, LocationWrapperPreservesSharedStructureAndBothObservations) {
    const auto mapper = locationMapper();
    il_message::DataMessageDTO message;
    message.data = {{"Vehicle.CurrentLocation.Latitude", 60.0},
                    {"Vehicle.CurrentLocation.Longitude", 10.0}};
    message.metadata =
        std::vector<il_data_point::MetadataPointDTO>{metadata("Vehicle.CurrentLocation.Latitude"),
                                                     metadata("Vehicle.CurrentLocation.Longitude")};
    const auto parsed = rdf_mapper_test::readSerializedRdf(
        mapper.signalToRdf(message, "VIN_test", GetParam()), GetParam());
    ASSERT_EQ(parsed.size(), 13U);
    expectTriples(parsed, mapper.signalToTriples(message, "VIN_test"));
}

TEST_P(RDFMapperSerializationTest, EmptySignalWrapperProducesAnEmptyGraph) {
    const auto mapper = speedMapper();
    const auto document = mapper.signalToRdf(il_message::DataMessageDTO{}, "VIN_test", GetParam());
    ASSERT_EQ(document, "");
    EXPECT_TRUE(rdf_mapper_test::readSerializedRdf(document, GetParam()).empty());
}

TEST_P(RDFMapperSerializationTest, WrappersPreserveMappingInputValidation) {
    const auto mapper = speedMapper();
    EXPECT_THROW(mapper.vehicleToRdf("", GetParam()), std::invalid_argument);
    EXPECT_THROW(mapper.signalToRdf(speedMessage(), "", GetParam()), std::invalid_argument);
    auto message = speedMessage();
    message.metadata.reset();
    EXPECT_THROW(mapper.signalToRdf(message, "VIN_test", GetParam()), std::invalid_argument);
    message = speedMessage();
    message.data.front().path = "Vehicle.Unknown";
    EXPECT_THROW(mapper.signalToRdf(message, "VIN_test", GetParam()), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(SupportedSyntaxes, RDFMapperSerializationTest,
                         ::testing::Values(RDFFormat::Turtle, RDFFormat::NTriples,
                                           RDFFormat::NQuads, RDFFormat::TriG),
                         [](const ::testing::TestParamInfo<RDFFormat>& info) {
                             switch (info.param) {
                                 case RDFFormat::Turtle:
                                     return "Turtle";
                                 case RDFFormat::NTriples:
                                     return "NTriples";
                                 case RDFFormat::NQuads:
                                     return "NQuads";
                                 case RDFFormat::TriG:
                                     return "TriG";
                             }
                             return "Unknown";
                         });

TEST_F(RDFMapperSignalTest, VehicleGraphContainsAllTypesAndForwardContainmentEdgesOnly) {
    const auto mapper = speedMapper();
    const auto triples = mapper.vehicleToTriples("VIN_test");
    std::vector<RDFTriple> expected;
    const std::vector<std::pair<std::string, std::string>> entities{
        {"", "Vehicle"},
        {"VehicleIdentification", "Vehicle_VehicleIdentification"},
        {"Powertrain", "Vehicle_Powertrain"},
        {"Powertrain/Transmission", "Vehicle_Powertrain_Transmission"},
        {"Powertrain/TractionBattery", "Vehicle_Powertrain_TractionBattery"},
        {"Powertrain/TractionBattery/StateOfCharge",
         "Vehicle_Powertrain_TractionBattery_StateOfCharge"},
        {"Ai", "Vehicle_Ai"},
        {"Ai/Reasoner", "Vehicle_Ai_Reasoner"},
        {"CurrentLocation", "Vehicle_CurrentLocation"},
        {"Chassis", "Vehicle_Chassis"},
        {"Chassis/SteeringWheel", "Vehicle_Chassis_SteeringWheel"}};
    for (const auto& entity : entities) {
        expected.push_back(
            {instance(entity.first), RDFNode::iri(kRdfType), RDFNode::iri(kVss + entity.second)});
    }
    const auto edge = [&expected](const std::string& parent, const std::string& predicate,
                                  const std::string& child) {
        expected.push_back({instance(parent), RDFNode::iri(kVss + predicate), instance(child)});
    };
    edge("", "hasVehicle_VehicleIdentification", "VehicleIdentification");
    edge("", "hasVehicle_Powertrain", "Powertrain");
    edge("", "hasVehicle_Ai", "Ai");
    edge("", "hasVehicle_CurrentLocation", "CurrentLocation");
    edge("", "hasVehicle_Chassis", "Chassis");
    edge("Ai", "hasVehicle_Ai_Reasoner", "Ai/Reasoner");
    edge("Chassis", "hasVehicle_Chassis_SteeringWheel", "Chassis/SteeringWheel");
    edge("Powertrain", "hasVehicle_Powertrain_Transmission", "Powertrain/Transmission");
    edge("Powertrain", "hasVehicle_Powertrain_TractionBattery", "Powertrain/TractionBattery");
    edge("Powertrain/TractionBattery", "hasVehicle_Powertrain_TractionBattery_StateOfCharge",
         "Powertrain/TractionBattery/StateOfCharge");
    ASSERT_EQ(expected.size(), 21U);
    expectTriples(triples, expected);
    EXPECT_TRUE(std::all_of(triples.begin(), triples.end(), [](const RDFTriple& triple) {
        return triple.object.type == RDFNodeType::Iri;
    }));
}

TEST_F(RDFMapperSignalTest, VehicleIdBytesAreEncodedAsOnePathComponent) {
    const auto mapper = speedMapper();
    const auto triples = mapper.vehicleToTriples("VIN /%#?\xC3\xA4");
    const auto root = instance("", "VIN%20%2F%25%23%3F%C3%A4");
    const RDFGraph graph(triples);
    EXPECT_EQ(graph.object(root, kRdfType), RDFNode::iri(kVss + "Vehicle"));
    EXPECT_EQ(graph.object(root, kVss + "hasVehicle_CurrentLocation"),
              instance("CurrentLocation", "VIN%20%2F%25%23%3F%C3%A4"));
    EXPECT_EQ(triples.size(), 21U);
}

TEST_F(RDFMapperSignalTest, RejectsEmptyVehicleIdForVehicleGraph) {
    const auto mapper = speedMapper();
    EXPECT_THROW(mapper.vehicleToTriples(""), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, SpeedUsesDeclaredPropertiesAndCallerVehicleId) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.metadata->front().timestamps.received = generatedTime() + std::chrono::hours{1};
    auto expected = observationTriples(observation("Speed"), instance(), "speed", "42.5", "float");
    expected.push_back({instance(), RDFNode::iri(kRdfType), RDFNode::iri(kVss + "Vehicle")});
    const auto actual = mapper.signalToTriples(message, "VIN_test");
    expectTriples(actual, expected);
    EXPECT_EQ(actual.size(), 6U);
}

TEST_F(RDFMapperSignalTest, MapsSuppliedSpeedWireFixtureThroughInformationLayerParser) {
    const auto mapper = speedMapper();
    const auto message = wireFixture("speed_message.json");
    ASSERT_EQ(message.instance, "VIN_1805");
    const std::string fixture_time = "2026-04-17T12:32:09.853211000Z";
    auto expected = observationTriples(observation("Speed", fixture_time), instance(), "speed",
                                       "42.5", "float", fixture_time);
    expected.push_back({instance(), RDFNode::iri(kRdfType), RDFNode::iri(kVss + "Vehicle")});
    expectTriples(mapper.signalToTriples(message, "VIN_test"), expected);
}

TEST_F(RDFMapperSignalTest, SuppliedLocationWireFixtureRetainsRelativePathsAndIsRejected) {
    const auto mapper = locationMapper();
    const auto message = wireFixture("location_message.json");
    ASSERT_EQ(message.data.size(), 2U);
    EXPECT_TRUE(std::all_of(message.data.begin(), message.data.end(), [](const auto& point) {
        return point.path.compare(0, 16, "CurrentLocation.") == 0;
    }));
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, LocationJoinsMetadataByExactPathAndDeduplicatesSharedVehicleBranch) {
    const auto mapper = locationMapper();
    il_message::DataMessageDTO message;
    message.data = {{"Vehicle.CurrentLocation.Latitude", 60.0},
                    {"Vehicle.CurrentLocation.Longitude", 10.0}};
    const auto later = generatedTime() + std::chrono::seconds{1};
    const std::string later_text = "1970-01-01T00:00:43.000000123Z";
    message.metadata = std::vector<il_data_point::MetadataPointDTO>{
        metadata("Vehicle.CurrentLocation.Longitude", later),
        metadata("Vehicle.CurrentLocation.Latitude")};
    auto expected = observationTriples(observation("CurrentLocation/Latitude"),
                                       instance("CurrentLocation"), "latitude", "60", "double");
    const auto longitude =
        observationTriples(observation("CurrentLocation/Longitude", later_text),
                           instance("CurrentLocation"), "longitude", "10", "double", later_text);
    expected.insert(expected.end(), longitude.begin(), longitude.end());
    expected.push_back({instance(), RDFNode::iri(kRdfType), RDFNode::iri(kVss + "Vehicle")});
    expected.push_back({instance("CurrentLocation"), RDFNode::iri(kRdfType),
                        RDFNode::iri(kVss + "Vehicle_CurrentLocation")});
    expected.push_back({instance(), RDFNode::iri(kVss + "hasVehicle_CurrentLocation"),
                        instance("CurrentLocation")});
    const auto triples = mapper.signalToTriples(message, "VIN_test");
    ASSERT_EQ(expected.size(), 13U);
    expectTriples(triples, expected);
}

TEST_F(RDFMapperSignalTest, EmptyDataNeedsNoMetadataAndEmitsNoVehicleGraph) {
    const auto mapper = speedMapper();
    const il_message::DataMessageDTO message;
    EXPECT_TRUE(mapper.signalToTriples(message, "VIN_test").empty());
}

TEST_F(RDFMapperSignalTest, EmptyDataStillRejectsDuplicateMetadataPaths) {
    const auto mapper = speedMapper();
    il_message::DataMessageDTO message;
    message.metadata = std::vector<il_data_point::MetadataPointDTO>{metadata("Vehicle.Speed"),
                                                                    metadata("Vehicle.Speed")};
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, ExactSignalLookupRejectsRelativeAndCaseChangedKeys) {
    const auto mapper = speedMapper();
    for (const std::string path :
         {"Speed", "vehicle.Speed", "Vehicle.speed", "Vehicle.Speed.Value"}) {
        auto message = speedMessage();
        message.data.front().path = path;
        message.metadata->front().path = path;
        EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument) << path;
    }
}

TEST_F(RDFMapperSignalTest, MissingOrEmptyMetadataIsRejected) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.metadata.reset();
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
    message.metadata = std::vector<il_data_point::MetadataPointDTO>{};
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, MetadataPathMustExactlyMatchTheDataPath) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.metadata->front().path = "Speed";
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, ReceivedOrRootGeneratedTimeCannotReplaceExactGeneratedTime) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    auto& timestamps = message.metadata->front().timestamps;
    timestamps.generated.reset();
    timestamps.received = generatedTime();
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
    message.metadata->push_back(metadata(""));
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, DuplicateMetadataPathsAreRejectedEvenWhenEqual) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.metadata->push_back(message.metadata->front());
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, ConflictingMetadataTimesAreRejected) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.metadata->push_back(
        metadata("Vehicle.Speed", generatedTime() + std::chrono::seconds{1}));
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, DuplicateUnusedMetadataPathsAreStillRejected) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.metadata->push_back(metadata("unrelated"));
    message.metadata->push_back(metadata("unrelated"));
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, DuplicateDataPathsAreRejectedEvenWhenEqual) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.data.push_back(message.data.front());
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, ConflictingDataValuesAreRejected) {
    const auto mapper = speedMapper();
    auto message = speedMessage();
    message.data.push_back({"Vehicle.Speed", 43.5});
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, RejectsEmptyVehicleIdForSignalGraph) {
    const auto mapper = speedMapper();
    EXPECT_THROW(mapper.signalToTriples(speedMessage(), ""), std::invalid_argument);
    EXPECT_THROW(mapper.signalToTriples(il_message::DataMessageDTO{}, ""), std::invalid_argument);
}

TEST_F(RDFMapperSignalTest, ReplayIsStableAcrossCallsAndIndependentMapperInstances) {
    const auto first = speedMapper();
    const auto second = speedMapper();
    const auto message = speedMessage();
    const auto original = first.signalToTriples(message, "VIN_test");
    expectTriples(first.signalToTriples(message, "VIN_test"), original);
    expectTriples(second.signalToTriples(message, "VIN_test"), original);
    const auto later = first.signalToTriples(
        speedMessage(42.5, generatedTime() + std::chrono::nanoseconds{1}), "VIN_test");
    EXPECT_NE(later, original);
}

TEST_F(RDFMapperSignalTest, ObservationAndFeatureShareEncodedCallerVehicleId) {
    const auto mapper = speedMapper();
    const auto triples = mapper.signalToTriples(speedMessage(), "VIN /%#?\xC3\xA4");
    const auto subject = observation("Speed", kGeneratedText, "VIN%20%2F%25%23%3F%C3%A4");
    const RDFGraph graph(triples);
    EXPECT_EQ(graph.object(subject, kSosa + "hasFeatureOfInterest"),
              instance("", "VIN%20%2F%25%23%3F%C3%A4"));
    EXPECT_EQ(graph.object(subject, kSosa + "resultTime"),
              RDFNode::literal(kGeneratedText, kXsd + "dateTime"));
    EXPECT_EQ(triples.size(), 6U);
}

TEST_F(RDFMapperSignalTest, RejectedBatchDoesNotCorruptLaterValidMapping) {
    const auto mapper = speedMapper();
    auto invalid = speedMessage();
    invalid.data.push_back({"Vehicle.Unknown", 1.0});
    invalid.metadata->push_back(metadata("Vehicle.Unknown"));
    EXPECT_THROW(mapper.signalToTriples(invalid, "VIN_test"), std::invalid_argument);
    EXPECT_EQ(mapper.signalToTriples(speedMessage(), "VIN_test").size(), 6U);
}

TEST_F(RDFMapperSignalTest, FloatConversionRoundsToItsDeclaredValueSpace) {
    const auto mapper = speedMapper();
    const RDFGraph graph(mapper.signalToTriples(speedMessage(0.1), "VIN_test"));
    const auto value = graph.object(observation("Speed"), kSosa + "hasSimpleResult");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->datatype, kXsd + "float");
    EXPECT_EQ(static_cast<float>(std::stod(value->value)), static_cast<float>(0.1));
}

TEST_F(RDFMapperSignalTest, FloatConversionAcceptsLargestAndSmallestRepresentableValues) {
    const auto mapper = speedMapper();
    for (const float input :
         {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()}) {
        const RDFGraph graph(
            mapper.signalToTriples(speedMessage(static_cast<double>(input)), "VIN_test"));
        const auto value = graph.object(observation("Speed"), kSosa + "hasSimpleResult");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(static_cast<float>(std::stod(value->value)), input);
    }
}

TEST_F(RDFMapperSignalTest, Int64ToFloatConversionAvoidsDoubleRounding) {
    const auto mapper = speedMapper();
    const std::int64_t input = (std::int64_t{1} << 53) + (std::int64_t{1} << 29) + 1;
    const RDFGraph graph(mapper.signalToTriples(speedMessage(input), "VIN_test"));
    const auto value = graph.object(observation("Speed"), kSosa + "hasSimpleResult");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->datatype, kXsd + "float");
    EXPECT_EQ(static_cast<float>(std::stod(value->value)), static_cast<float>(input));
}

TEST_F(RDFMapperSignalTest, DoubleSignalRetainsPrecisionAndAcceptsInt64NumericValues) {
    const auto mapper = locationMapper();
    for (const Scalar& input :
         {Scalar{60.12345678901234}, Scalar{std::numeric_limits<std::int64_t>::max()}}) {
        il_message::DataMessageDTO message;
        message.data.push_back({"Vehicle.CurrentLocation.Latitude", input});
        message.metadata = std::vector<il_data_point::MetadataPointDTO>{
            metadata("Vehicle.CurrentLocation.Latitude")};
        const RDFGraph graph(mapper.signalToTriples(message, "VIN_test"));
        const auto value =
            graph.object(observation("CurrentLocation/Latitude"), kSosa + "hasSimpleResult");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->datatype, kXsd + "double");
        const double expected = std::holds_alternative<double>(input)
                                    ? std::get<double>(input)
                                    : static_cast<double>(std::get<std::int64_t>(input));
        EXPECT_DOUBLE_EQ(std::stod(value->value), expected);
    }
}

TEST_F(RDFMapperSignalTest, NumericShapesRejectStringAndBooleanValues) {
    const auto speed = speedMapper();
    const auto location = locationMapper();
    for (const Scalar& input : {Scalar{std::string{"42.5"}}, Scalar{true}}) {
        EXPECT_THROW(speed.signalToTriples(speedMessage(input), "VIN_test"), std::invalid_argument);
        auto message = speedMessage(input);
        message.data.front().path = "Vehicle.CurrentLocation.Latitude";
        message.metadata->front().path = message.data.front().path;
        EXPECT_THROW(location.signalToTriples(message, "VIN_test"), std::invalid_argument);
    }
}

struct InvalidNumericValue {
    const char* name;
    double value;
};

void PrintTo(const InvalidNumericValue& value, std::ostream* out) { *out << value.name; }

class RDFFloatValidationTest : public RDFMapperSignalTest,
                               public ::testing::WithParamInterface<InvalidNumericValue> {};

TEST_P(RDFFloatValidationTest, RejectsNonFiniteOverflowAndUnderflow) {
    const auto mapper = speedMapper();
    EXPECT_THROW(mapper.signalToTriples(speedMessage(GetParam().value), "VIN_test"),
                 std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    FloatValue, RDFFloatValidationTest,
    ::testing::Values(
        InvalidNumericValue{"NaN", std::numeric_limits<double>::quiet_NaN()},
        InvalidNumericValue{"PositiveInfinity", std::numeric_limits<double>::infinity()},
        InvalidNumericValue{"NegativeInfinity", -std::numeric_limits<double>::infinity()},
        InvalidNumericValue{"PositiveOverflow",
                            static_cast<double>(std::numeric_limits<float>::max()) * 2.0},
        InvalidNumericValue{"NegativeOverflow",
                            -static_cast<double>(std::numeric_limits<float>::max()) * 2.0},
        InvalidNumericValue{"PositiveUnderflow",
                            static_cast<double>(std::numeric_limits<float>::denorm_min()) / 4.0},
        InvalidNumericValue{"NegativeUnderflow",
                            -static_cast<double>(std::numeric_limits<float>::denorm_min()) / 4.0}),
    [](const ::testing::TestParamInfo<InvalidNumericValue>& info) { return info.param.name; });

class RDFDoubleValidationTest : public RDFMapperSignalTest,
                                public ::testing::WithParamInterface<InvalidNumericValue> {};

TEST_P(RDFDoubleValidationTest, RejectsNonFiniteValues) {
    const auto mapper = locationMapper();
    auto message = speedMessage(GetParam().value);
    message.data.front().path = "Vehicle.CurrentLocation.Latitude";
    message.metadata->front().path = message.data.front().path;
    EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    DoubleValue, RDFDoubleValidationTest,
    ::testing::Values(
        InvalidNumericValue{"NaN", std::numeric_limits<double>::quiet_NaN()},
        InvalidNumericValue{"PositiveInfinity", std::numeric_limits<double>::infinity()},
        InvalidNumericValue{"NegativeInfinity", -std::numeric_limits<double>::infinity()}),
    [](const ::testing::TestParamInfo<InvalidNumericValue>& info) { return info.param.name; });

struct GeneratedTimeCase {
    const char* name;
    TimePoint value;
    const char* text;
};

void PrintTo(const GeneratedTimeCase& value, std::ostream* out) { *out << value.name; }

class RDFGeneratedTimeTest : public RDFMapperSignalTest,
                             public ::testing::WithParamInterface<GeneratedTimeCase> {};

TEST_P(RDFGeneratedTimeTest, UsesGeneratedUtcNanosecondsForLiteralAndObservationIdentity) {
    const auto mapper = speedMapper();
    const auto triples = mapper.signalToTriples(speedMessage(42.5, GetParam().value), "VIN_test");
    const RDFGraph graph(triples);
    const auto subject = observation("Speed", GetParam().text);
    EXPECT_EQ(graph.object(subject, kRdfType), RDFNode::iri(kSosa + "Observation"));
    EXPECT_EQ(graph.object(subject, kSosa + "resultTime"),
              RDFNode::literal(GetParam().text, kXsd + "dateTime"));
    EXPECT_EQ(triples.size(), 6U);
}

// The supported Linux toolchain uses a signed 64-bit nanosecond system_clock.
INSTANTIATE_TEST_SUITE_P(
    GeneratedUtc, RDFGeneratedTimeTest,
    ::testing::Values(
        GeneratedTimeCase{"Epoch", TimePoint{}, "1970-01-01T00:00:00.000000000Z"},
        GeneratedTimeCase{"NegativeNanosecond", TimePoint{std::chrono::nanoseconds{-1}},
                          "1969-12-31T23:59:59.999999999Z"},
        GeneratedTimeCase{"NegativeSecondWithNanoseconds",
                          TimePoint{std::chrono::seconds{-1} + std::chrono::nanoseconds{123}},
                          "1969-12-31T23:59:59.000000123Z"},
        GeneratedTimeCase{
            "FixtureGeneratedTime",
            TimePoint{std::chrono::seconds{1776429129} + std::chrono::nanoseconds{853211000}},
            "2026-04-17T12:32:09.853211000Z"},
        GeneratedTimeCase{"MinimumClockValue", TimePoint::min(), "1677-09-21T00:12:43.145224192Z"},
        GeneratedTimeCase{"MaximumClockValue", TimePoint::max(), "2262-04-11T23:47:16.854775807Z"}),
    [](const ::testing::TestParamInfo<GeneratedTimeCase>& info) { return info.param.name; });

// The IRI segment follows the signal's target class, not a fixed SOSA vocabulary word.
TEST_F(RDFMapperSignalTest, SignalIriSegmentFollowsTargetClassLocalName) {
    const RDFMapper mapper{vehicle_shapes_,
                           fixtures_ / "signal_shacl_custom_class" / "speed_signal.ttl", kBase};
    const auto triples = mapper.signalToTriples(speedMessage(), "VIN_test");
    const RDFNode subject = RDFNode::iri(instance().value + "/signal/Speed/" + kGeneratedText);
    const RDFGraph graph(triples);
    EXPECT_EQ(graph.object(subject, kRdfType), RDFNode::iri("https://example.org/ontology#Signal"));
    EXPECT_EQ(graph.object(subject, "https://example.org/ontology#value"),
              RDFNode::literal("42.5", kXsd + "float"));
}

struct IntegerSignalCase {
    const char* name;
    const char* path;
    Scalar value;
    bool accepted;
};

void PrintTo(const IntegerSignalCase& value, std::ostream* out) { *out << value.name; }

class RDFIntegerDatatypeTest : public RDFMapperSignalTest,
                               public ::testing::WithParamInterface<IntegerSignalCase> {
   protected:
    RDFMapper integerMapper() const {
        return RDFMapper{vehicle_shapes_, fixtures_ / "signal_shacl_datatypes" / "integer_obs.ttl",
                         kBase};
    }
};

// xsd:integer-derived datatypes accept in-range int64 values and keep their declared type.
TEST_P(RDFIntegerDatatypeTest, EnforcesDerivedIntegerRange) {
    const auto mapper = integerMapper();
    const auto& param = GetParam();
    il_message::DataMessageDTO message;
    message.data.push_back({param.path, param.value});
    message.metadata = std::vector<il_data_point::MetadataPointDTO>{metadata(param.path)};
    if (!param.accepted) {
        EXPECT_THROW(mapper.signalToTriples(message, "VIN_test"), std::invalid_argument);
        return;
    }

    std::string relative_path = std::string{param.path}.substr(std::string{"Vehicle."}.size());
    std::replace(relative_path.begin(), relative_path.end(), '.', '/');
    const RDFGraph graph(mapper.signalToTriples(message, "VIN_test"));
    const auto value = graph.object(observation(relative_path), kSosa + "hasSimpleResult");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->value, std::to_string(std::get<std::int64_t>(param.value)));
}

TEST_F(RDFMapperSignalTest, UnsignedByteSignalUsesDeclaredDatatypeAndFeature) {
    const RDFMapper mapper{vehicle_shapes_,
                           fixtures_ / "signal_shacl_datatypes" / "integer_obs.ttl", kBase};
    const std::string path = "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current";
    il_message::DataMessageDTO message;
    message.data.push_back({path, std::int64_t{87}});
    message.metadata = std::vector<il_data_point::MetadataPointDTO>{metadata(path)};
    const RDFGraph graph(mapper.signalToTriples(message, "VIN_test"));
    const auto subject = observation("Powertrain/TractionBattery/StateOfCharge/Current");
    EXPECT_EQ(graph.object(subject, kSosa + "hasSimpleResult"),
              RDFNode::literal("87", kXsd + "unsignedByte"));
    EXPECT_EQ(graph.object(subject, kSosa + "hasFeatureOfInterest"),
              instance("Powertrain/TractionBattery/StateOfCharge"));
}

INSTANTIATE_TEST_SUITE_P(
    DerivedInteger, RDFIntegerDatatypeTest,
    ::testing::Values(
        IntegerSignalCase{"UnsignedByteMin",
                          "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current",
                          Scalar{std::int64_t{0}}, true},
        IntegerSignalCase{"UnsignedByteMax",
                          "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current",
                          Scalar{std::int64_t{255}}, true},
        IntegerSignalCase{"UnsignedByteOverflow",
                          "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current",
                          Scalar{std::int64_t{256}}, false},
        IntegerSignalCase{"UnsignedByteNegative",
                          "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current",
                          Scalar{std::int64_t{-1}}, false},
        IntegerSignalCase{"UnsignedByteRejectsDouble",
                          "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current",
                          Scalar{87.0}, false},
        IntegerSignalCase{"IntMin", "Vehicle.Test.Int",
                          Scalar{std::int64_t{std::numeric_limits<std::int32_t>::min()}}, true},
        IntegerSignalCase{"IntOverflow", "Vehicle.Test.Int",
                          Scalar{std::int64_t{std::numeric_limits<std::int32_t>::max()} + 1},
                          false},
        IntegerSignalCase{"ShortMax", "Vehicle.Test.Short",
                          Scalar{std::int64_t{std::numeric_limits<std::int16_t>::max()}}, true},
        IntegerSignalCase{"ShortUnderflow", "Vehicle.Test.Short",
                          Scalar{std::int64_t{std::numeric_limits<std::int16_t>::min()} - 1},
                          false},
        IntegerSignalCase{"NegativeIntegerMax", "Vehicle.Test.NegativeInteger",
                          Scalar{std::int64_t{-1}}, true},
        IntegerSignalCase{"NegativeIntegerRejectsZero", "Vehicle.Test.NegativeInteger",
                          Scalar{std::int64_t{0}}, false},
        IntegerSignalCase{"UnsignedLongMax", "Vehicle.Test.UnsignedLong",
                          Scalar{std::numeric_limits<std::int64_t>::max()}, true},
        IntegerSignalCase{"UnsignedLongNegative", "Vehicle.Test.UnsignedLong",
                          Scalar{std::int64_t{-1}}, false}),
    [](const ::testing::TestParamInfo<IntegerSignalCase>& info) { return info.param.name; });

}  // namespace
