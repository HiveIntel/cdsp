#include "reasoner_profile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

std::string canonicalReasonerName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (name == "rdf4j") {
        return "RDF4J";
    }
    if (name == "cqels") {
        return "CQELS";
    }

    throw std::invalid_argument("Unsupported reasoner profile name: " + name);
}

std::filesystem::path resolveProfileResource(const std::string& value,
                                             const std::filesystem::path& profile_directory,
                                             const std::string& field_name) {
    if (value.empty()) {
        throw std::invalid_argument("Reasoner profile field '" + field_name + "' cannot be empty.");
    }

    std::filesystem::path resource_path(value);
    if (resource_path.is_relative()) {
        resource_path = profile_directory / resource_path;
    }
    resource_path = resource_path.lexically_normal();

    if (!std::filesystem::is_regular_file(resource_path)) {
        throw std::invalid_argument("Reasoner profile field '" + field_name + "' refers to a missing file: " +
                                    resource_path.string());
    }

    return resource_path;
}

std::vector<std::filesystem::path> parseProfileResources(
    const nlohmann::json& profile_json,
    const std::string& field_name,
    const std::filesystem::path& profile_directory,
    bool required) {
    if (!profile_json.contains(field_name)) {
        if (required) {
            throw std::invalid_argument("Reasoner profile is missing required field '" + field_name + "'.");
        }
        return {};
    }

    const auto& field_value = profile_json.at(field_name);
    std::vector<std::filesystem::path> resources;

    if (field_value.is_string()) {
        resources.push_back(resolveProfileResource(field_value.get<std::string>(), profile_directory, field_name));
        return resources;
    }

    if (!field_value.is_array() || field_value.empty()) {
        throw std::invalid_argument("Reasoner profile field '" + field_name +
                                    "' must be a non-empty string or array of strings.");
    }

    for (const auto& resource : field_value) {
        if (!resource.is_string()) {
            throw std::invalid_argument("Reasoner profile field '" + field_name +
                                        "' must contain only strings.");
        }
        resources.push_back(
            resolveProfileResource(resource.get<std::string>(), profile_directory, field_name));
    }

    return resources;
}

ReasonerProfile::Endpoint parseEndpoint(const nlohmann::json& profile_json) {
    ReasonerProfile::Endpoint endpoint;
    if (!profile_json.contains("endpoint")) {
        return endpoint;
    }

    const auto& endpoint_json = profile_json.at("endpoint");
    if (!endpoint_json.is_object()) {
        throw std::invalid_argument("Reasoner profile field 'endpoint' must be an object.");
    }
    if (endpoint_json.contains("host")) {
        if (!endpoint_json.at("host").is_string() || endpoint_json.at("host").get<std::string>().empty()) {
            throw std::invalid_argument("Reasoner endpoint host must be a non-empty string.");
        }
        endpoint.host = endpoint_json.at("host").get<std::string>();
    }
    if (endpoint_json.contains("port")) {
        if (!endpoint_json.at("port").is_number_unsigned()) {
            throw std::invalid_argument("Reasoner endpoint port must be an unsigned integer.");
        }
        const auto port = endpoint_json.at("port").get<unsigned int>();
        if (port == 0 || port > 65535) {
            throw std::invalid_argument("Reasoner endpoint port must be in range 1..65535.");
        }
        endpoint.port = static_cast<unsigned short>(port);
    }
    if (endpoint_json.contains("datastore")) {
        if (!endpoint_json.at("datastore").is_string() ||
            endpoint_json.at("datastore").get<std::string>().empty()) {
            throw std::invalid_argument("Reasoner endpoint datastore must be a non-empty string.");
        }
        endpoint.datastore = endpoint_json.at("datastore").get<std::string>();
    }
    return endpoint;
}

}  // namespace

ReasonerProfile ReasonerProfileParser::parse(const std::filesystem::path& profile_file_path) {
    std::filesystem::path resolved_profile_path = profile_file_path;
    if (resolved_profile_path.is_relative()) {
        resolved_profile_path = std::filesystem::absolute(resolved_profile_path);
    }
    resolved_profile_path = resolved_profile_path.lexically_normal();

    if (!std::filesystem::is_regular_file(resolved_profile_path)) {
        throw std::invalid_argument("Reasoner profile file does not exist: " +
                                    resolved_profile_path.string());
    }

    std::ifstream profile_stream(resolved_profile_path);
    if (!profile_stream) {
        throw std::runtime_error("Unable to read reasoner profile: " + resolved_profile_path.string());
    }

    try {
        const nlohmann::json profile_json = nlohmann::json::parse(profile_stream);
        if (!profile_json.is_object()) {
            throw std::invalid_argument("Reasoner profile must be a JSON object.");
        }
        if (!profile_json.contains("name") || !profile_json.at("name").is_string() ||
            profile_json.at("name").get<std::string>().empty()) {
            throw std::invalid_argument("Reasoner profile requires a non-empty string field 'name'.");
        }

        ReasonerProfile profile;
        profile.name = canonicalReasonerName(profile_json.at("name").get<std::string>());
        profile.ontologies = parseProfileResources(profile_json, "ontologies",
                                                    resolved_profile_path.parent_path(), false);
        profile.rules = parseProfileResources(profile_json, "rules",
                                              resolved_profile_path.parent_path(), false);
        profile.queries = parseProfileResources(profile_json, "query",
                                                resolved_profile_path.parent_path(), true);
        profile.endpoint = parseEndpoint(profile_json);
        return profile;
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument("Invalid reasoner profile JSON: " + std::string(error.what()));
    }
}
