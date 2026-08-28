#include "model_config_parser.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

void requireObject(const json& value, const char* field_name) {
    if (!value.contains(field_name) || !value.at(field_name).is_object()) {
        throw std::invalid_argument(std::string("Missing required object field: ") + field_name);
    }
}

}  // namespace

ModelConfig ModelConfigParser::parseFile(const std::filesystem::path& config_file) {
    const std::filesystem::path resolved_config_file =
        std::filesystem::absolute(config_file).lexically_normal();
    if (!std::filesystem::is_regular_file(resolved_config_file)) {
        throw std::invalid_argument("Model configuration file does not exist: " +
                                    resolved_config_file.string());
    }

    std::ifstream config_stream(resolved_config_file);
    if (!config_stream) {
        throw std::runtime_error("Unable to read model configuration file: " +
                                 resolved_config_file.string());
    }

    try {
        const json config = json::parse(config_stream);
        requireObject(config, "input");
        const json& input = config.at("input");

        if (!input.contains("signals") || !input.at("signals").is_array()) {
            throw std::invalid_argument("Missing required input.signals array.");
        }
        if (!input.contains("rdf_shapes") || !input.at("rdf_shapes").is_object()) {
            throw std::invalid_argument("Missing required input.rdf_shapes object.");
        }
        if (!config.contains("reasoner_profile") || !config.at("reasoner_profile").is_string()) {
            throw std::invalid_argument("Missing required reasoner_profile string.");
        }

        std::map<std::string, std::string> rdf_shapes;
        for (const auto& [shape_name, shape_path] : input.at("rdf_shapes").items()) {
            if (!shape_path.is_string()) {
                throw std::invalid_argument("RDF shape '" + shape_name + "' must be a string.");
            }
            rdf_shapes.emplace(shape_name, shape_path.get<std::string>());
        }

        return {input.at("signals").get<std::vector<std::string>>(),
                std::move(rdf_shapes),
                config.at("reasoner_profile").get<std::string>()};
    } catch (const json::exception& error) {
        throw std::invalid_argument("Invalid model configuration JSON: " +
                                    std::string(error.what()));
    }
}
