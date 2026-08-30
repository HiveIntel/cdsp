#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "il_client.h"
#include "model_config_parser.h"
#include "rdf_mapper.h"
#include "reasoner_factory.h"

namespace {

std::string environmentOrDefault(const char* name, const char* default_value) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : default_value;
}

std::filesystem::path resolve(const std::filesystem::path& base, const std::string& value) {
    const std::filesystem::path path(value);
    return (path.is_absolute() ? path : base / path).lexically_normal();
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::filesystem::path model_file = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path(environmentOrDefault(
                  "MODEL_CONFIG", "examples/driving-style/model/driving_style_reasoning.json"));
        const ModelConfig model = ModelConfigParser::parseFile(model_file);
        const auto model_directory = std::filesystem::absolute(model_file).parent_path();

        std::vector<std::filesystem::path> shape_paths;
        for (const auto& [_, shape] : model.getRdfShapes()) {
            shape_paths.push_back(resolve(model_directory, shape));
        }
        const auto mapper = std::make_shared<rdf_mapper::RdfMapper>(
            shape_paths, model.getSignals(), rdf_mapper::RdfFormat::Turtle);
        const auto reasoner = ReasonerFactory::initReasoner(
            resolve(model_directory, model.getReasonerProfile()));
        const auto client = std::make_shared<InformationLayerClient>(
            environmentOrDefault("HOST_WEBSOCKET_SERVER", "127.0.0.1"),
            environmentOrDefault("PORT_WEBSOCKET_SERVER", "8080"),
            mapper, 
            model.getSignals(), 
            reasoner);

            

        client->initializeConnection();
        client->run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Driving-style client failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
