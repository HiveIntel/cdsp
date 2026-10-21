#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model_config_parser.h"

namespace {

const std::filesystem::path kDrivingStyleConfig =
    std::filesystem::path(PROJECT_ROOT).parent_path().parent_path() /
    "examples/driving-style/model/driving_style_reasoning.json";

}  // namespace

TEST(ModelConfigParserTest, ParsesDrivingStyleConfiguration) {
    const ModelConfig configuration = ModelConfigParser::parseFile(kDrivingStyleConfig);

    EXPECT_EQ(configuration.getSignals(),
              (std::vector<std::string>{"Chassis.SteeringWheel.Angle",
                                        "CurrentLocation.Latitude",
                                        "CurrentLocation.Longitude",
                                        "Speed"}));
    EXPECT_EQ(configuration.getRdfShapes(),
              (std::map<std::string, std::string>{{"signal", "shapes/signals_shapes.shacl.ttl"},
                                                  {"vehicle", "shapes/vehicle_shape.shacl.ttl"}}));
    EXPECT_EQ(configuration.getReasonerProfile(),
              "../solutions/rdf4j/driving_style_rdf4j.json");
}

TEST(ModelConfigParserTest, RejectsMissingRequiredFields) {
    EXPECT_THROW(ModelConfig({}, {{"vehicle", "vehicle.ttl"}}, "profile.json"),
                 std::invalid_argument);
}
