#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "rdf_mapper.h"
#include "serd_rdf_reader.h"

namespace {

using rdf_mapper::RDFMapper;

constexpr const char* kInstanceBaseIri = "https://example.org/instances/";

class RDFMapperConstructorTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Fixture content is supplied by the user; tests never create or modify TTL.
        ASSERT_TRUE(std::filesystem::is_regular_file(vehicle_shapes_file_))
            << "Missing vehicle fixture: " << vehicle_shapes_file_;
        ASSERT_TRUE(std::filesystem::is_regular_file(signal_shapes_file_))
            << "Missing signal fixture: " << signal_shapes_file_;
    }

    static void expectFileDiagnostic(const std::filesystem::path& vehicle_shapes,
                                     const std::filesystem::path& signal_shapes,
                                     const std::filesystem::path& invalid_file) {
        try {
            const RDFMapper mapper{vehicle_shapes, signal_shapes, kInstanceBaseIri};
            FAIL() << "Expected failure for invalid input file " << invalid_file;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string{error.what()}.find(invalid_file.string()), std::string::npos)
                << error.what();
        } catch (const std::exception& error) {
            FAIL() << "Unexpected exception type: " << error.what();
        }
    }

    const std::filesystem::path fixture_directory_{RDF_MAPPER_TEST_FIXTURE_DIR};
    const std::filesystem::path vehicle_shapes_file_{fixture_directory_ / "vehicle_shacl" /
                                                     "vehicle_kb.ttl"};
    const std::filesystem::path signal_shapes_file_{fixture_directory_ / "signal_shacl" /
                                                    "speed_obs.ttl"};
};

TEST_F(RDFMapperConstructorTest, LoadsVehicleAndSignalShapes) {
    EXPECT_NO_THROW((RDFMapper{vehicle_shapes_file_, signal_shapes_file_, kInstanceBaseIri}));
}

TEST_F(RDFMapperConstructorTest, LoadsCurrentLocationSignalShapes) {
    const auto location_shapes = fixture_directory_ / "signal_shacl" / "current_location_obs.ttl";
    ASSERT_TRUE(std::filesystem::is_regular_file(location_shapes))
        << "Missing location fixture: " << location_shapes;
    EXPECT_NO_THROW((RDFMapper{vehicle_shapes_file_, location_shapes, kInstanceBaseIri}));
}

TEST_F(RDFMapperConstructorTest, AcceptsHttpInstanceNamespace) {
    EXPECT_NO_THROW(
        (RDFMapper{vehicle_shapes_file_, signal_shapes_file_, "http://example.org/instances/"}));
}

TEST_F(RDFMapperConstructorTest, AcceptsPercentEncodedNamespacePath) {
    EXPECT_NO_THROW((RDFMapper{vehicle_shapes_file_, signal_shapes_file_,
                               "https://example.org/fleet%20one/instances/"}));
}

TEST_F(RDFMapperConstructorTest, AcceptsDecimalPortInInstanceNamespace) {
    EXPECT_NO_THROW((RDFMapper{vehicle_shapes_file_, signal_shapes_file_,
                               "https://example.org:8443/instances/"}));
}

TEST_F(RDFMapperConstructorTest, ReportsMissingVehicleFilePath) {
    const auto missing_vehicle = fixture_directory_ / "missing-vehicle.ttl";
    ASSERT_FALSE(std::filesystem::exists(missing_vehicle));
    expectFileDiagnostic(missing_vehicle, signal_shapes_file_, missing_vehicle);
}

TEST_F(RDFMapperConstructorTest, ReportsMissingSignalFilePath) {
    const auto missing_signal = fixture_directory_ / "missing-signal.ttl";
    ASSERT_FALSE(std::filesystem::exists(missing_signal));
    expectFileDiagnostic(vehicle_shapes_file_, missing_signal, missing_signal);
}

TEST_F(RDFMapperConstructorTest, RejectsVehicleDirectoryWithPathDiagnostic) {
    expectFileDiagnostic(fixture_directory_, signal_shapes_file_, fixture_directory_);
}

TEST_F(RDFMapperConstructorTest, RejectsSignalDirectoryWithPathDiagnostic) {
    expectFileDiagnostic(vehicle_shapes_file_, fixture_directory_, fixture_directory_);
}

TEST_F(RDFMapperConstructorTest, SignalSerializationDefaultsToRoundtrippableTurtle) {
    const RDFMapper mapper{vehicle_shapes_file_, signal_shapes_file_, kInstanceBaseIri};
    il_message::DataMessageDTO message;
    message.data = {{"Vehicle.Speed", 42.5}};
    il_data_point::TimestampsDTO timestamps;
    timestamps.generated = std::chrono::system_clock::time_point{std::chrono::seconds{42}};
    message.metadata = std::vector<il_data_point::MetadataPointDTO>{{"Vehicle.Speed", timestamps}};
    const auto expected = mapper.signalToTriples(message, "VIN_test");
    const auto parsed = rdf_mapper::SerdRdfReader{}.read(mapper.signalToRdf(message, "VIN_test"));
    ASSERT_EQ(parsed.size(), 6U);
    ASSERT_EQ(parsed.size(), expected.size());
    for (const auto& triple : expected) {
        EXPECT_EQ(std::count(parsed.begin(), parsed.end(), triple), 1) << triple;
    }
}

TEST_F(RDFMapperConstructorTest, VehicleSerializationDefaultsToRoundtrippableTurtle) {
    const RDFMapper mapper{vehicle_shapes_file_, signal_shapes_file_, kInstanceBaseIri};
    const auto expected = mapper.vehicleToTriples("VIN_test");
    const auto parsed = rdf_mapper::SerdRdfReader{}.read(mapper.vehicleToRdf("VIN_test"));
    ASSERT_EQ(parsed.size(), 21U);
    ASSERT_EQ(parsed.size(), expected.size());
    for (const auto& triple : expected) {
        EXPECT_EQ(std::count(parsed.begin(), parsed.end(), triple), 1) << triple;
    }
}

struct InvalidInstanceBaseIri {
    const char* name;
    std::string iri;
};

class RDFMapperNamespaceValidationTest
    : public RDFMapperConstructorTest,
      public ::testing::WithParamInterface<InvalidInstanceBaseIri> {};

TEST_P(RDFMapperNamespaceValidationTest, RejectsInvalidNamespace) {
    EXPECT_THROW((RDFMapper{vehicle_shapes_file_, signal_shapes_file_, GetParam().iri}),
                 std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    InstanceNamespace, RDFMapperNamespaceValidationTest,
    ::testing::Values(
        InvalidInstanceBaseIri{"Empty", ""}, InvalidInstanceBaseIri{"Relative", "instances/"},
        InvalidInstanceBaseIri{"UnsupportedScheme", "ftp://example.org/instances/"},
        InvalidInstanceBaseIri{"MissingAuthority", "https:///instances/"},
        InvalidInstanceBaseIri{"MissingHost", "https://:/"},
        InvalidInstanceBaseIri{"NonNumericPort", "https://example.org:abc/"},
        InvalidInstanceBaseIri{"EmptyPort", "https://example.org:/"},
        InvalidInstanceBaseIri{"MissingTrailingSlash", "https://example.org/instances"},
        InvalidInstanceBaseIri{"RawPathBrackets", "https://example.org/a[b]/"},
        InvalidInstanceBaseIri{"ParentPathSegment", "https://example.org/a/../"},
        InvalidInstanceBaseIri{"CurrentPathSegment", "https://example.org/a/./"},
        InvalidInstanceBaseIri{"Query", "https://example.org/instances/?version=1/"},
        InvalidInstanceBaseIri{"Fragment", "https://example.org/instances/#fragment/"},
        InvalidInstanceBaseIri{"LonePercentEscape", "https://example.org/instances/%/"},
        InvalidInstanceBaseIri{"IncompletePercentEscape", "https://example.org/instances/%2/"},
        InvalidInstanceBaseIri{"NonHexPercentEscape", "https://example.org/instances/%2G/"},
        InvalidInstanceBaseIri{"UnencodedSpace", "https://example.org/in stances/"},
        InvalidInstanceBaseIri{"UnencodedTab", "https://example.org/in\tstances/"},
        InvalidInstanceBaseIri{"UnencodedLineFeed", "https://example.org/in\nstances/"},
        InvalidInstanceBaseIri{"EmbeddedNull",
                               std::string{"https://example.org/in"} + '\0' + "stances/"},
        InvalidInstanceBaseIri{"UnencodedNonAscii", "https://example.org/inst\xC3\xA4nces/"},
        InvalidInstanceBaseIri{"LessThan", "https://example.org/in<stances/"},
        InvalidInstanceBaseIri{"GreaterThan", "https://example.org/in>stances/"},
        InvalidInstanceBaseIri{"DoubleQuote", "https://example.org/in\"stances/"},
        InvalidInstanceBaseIri{"Backslash", "https://example.org/in\\stances/"},
        InvalidInstanceBaseIri{"OpenBrace", "https://example.org/in{stances/"},
        InvalidInstanceBaseIri{"CloseBrace", "https://example.org/in}stances/"},
        InvalidInstanceBaseIri{"VerticalBar", "https://example.org/in|stances/"},
        InvalidInstanceBaseIri{"Caret", "https://example.org/in^stances/"},
        InvalidInstanceBaseIri{"Backtick", "https://example.org/in`stances/"}),
    [](const ::testing::TestParamInfo<InvalidInstanceBaseIri>& info) { return info.param.name; });

struct InvalidSignalShape {
    const char* fixture;
    const char* diagnostic;
};

void PrintTo(const InvalidSignalShape& shape, std::ostream* out) { *out << shape.fixture; }

class RDFMapperSignalShapeValidationTest
    : public RDFMapperConstructorTest,
      public ::testing::WithParamInterface<InvalidSignalShape> {};

// Malformed signal bindings must fail at construction, not when a message arrives.
TEST_P(RDFMapperSignalShapeValidationTest, RejectsMalformedSignalBinding) {
    const auto signal_shapes = fixture_directory_ / "signal_shacl_invalid" /
                               (std::string{GetParam().fixture} + ".ttl");
    ASSERT_TRUE(std::filesystem::is_regular_file(signal_shapes))
        << "Missing invalid signal fixture: " << signal_shapes;
    try {
        const RDFMapper mapper{vehicle_shapes_file_, signal_shapes, kInstanceBaseIri};
        FAIL() << "Expected construction to reject " << signal_shapes;
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string{error.what()}.find(GetParam().diagnostic), std::string::npos)
            << error.what();
    }
}

INSTANTIATE_TEST_SUITE_P(
    SignalBinding, RDFMapperSignalShapeValidationTest,
    ::testing::Values(
        InvalidSignalShape{"constraint_only_property", "exactly one value binding"},
        InvalidSignalShape{"multiple_bindings", "exactly one value binding"},
        InvalidSignalShape{"node_binding_with_datatype", "cannot have sh:datatype"},
        InvalidSignalShape{"blank_node_constant", "Blank-node constants"},
        InvalidSignalShape{"constant_datatype_conflict", "conflicts with sh:datatype"},
        InvalidSignalShape{"dynamic_without_datatype", "requires sh:datatype"},
        InvalidSignalShape{"unsupported_value_datatype", "Unsupported signal value datatype"},
        InvalidSignalShape{"unknown_value_selector", "Unsupported vss:value selector"},
        InvalidSignalShape{"timestamp_not_datetime", "requires xsd:dateTime"}),
    [](const ::testing::TestParamInfo<InvalidSignalShape>& info) {
        return std::string{info.param.fixture};
    });

}  // namespace
