#include "system_configuration_service.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

#include "dto_service.h"
#include "file_handler_impl.h"
#include "helper.h"
#include "model_config_dto.h"

SystemConfig SystemConfigurationService::loadSystemConfig(
    const std::optional<std::string> ws_server_host,
    const std::optional<std::string> ws_server_port,
    const std::optional<std::string> ws_server_target
    // TODO(system-configuration): Move these parameters to a dedicated reasoner service.
    // const std::optional<std::string> reasoner_server_host,
    // const std::optional<std::string> reasoner_server_port,
    // const std::optional<std::string> reasoner_server_auth_base64,
    // const std::optional<std::string> reasoner_server_data_store_name,
    // const std::optional<std::string>& reasoner_server_origin_system
) {
    SystemConfig system_config;
    system_config.websocket_server.host =
        Helper::getEnvVariable("HOST_WEBSOCKET_SERVER", ws_server_host);
    system_config.websocket_server.port =
        Helper::getEnvVariable("PORT_WEBSOCKET_SERVER", ws_server_port);
    system_config.websocket_server.target =
        Helper::getEnvVariable("TARGET_WEBSOCKET_SERVER", ws_server_target);
    // TODO(system-configuration): Move reasoner environment loading to a dedicated reasoner service.
    // system_config.reasoner_server.host =
    //     Helper::getEnvVariable("HOST_REASONER_SERVER", reasoner_server_host);
    // system_config.reasoner_server.port =
    //     Helper::getEnvVariable("PORT_REASONER_SERVER", reasoner_server_port);
    // system_config.reasoner_server.auth_base64 =
    //     Helper::getEnvVariable("AUTH_REASONER_SERVER_BASE64", reasoner_server_auth_base64);
    // system_config.reasoner_server.data_store_name =
    //     Helper::getEnvVariable("REASONER_DATASTORE", reasoner_server_data_store_name);
    // system_config.reasoner_server.origin_system_name =
    //     Helper::getEnvVariable("REASONER_ORIGIN_SYSTEM_NAME", reasoner_server_origin_system);

    return system_config;
}

ModelConfig SystemConfigurationService::loadModelConfig(const std::string& config_file) {
    // TODO(system-configuration): Move this implementation to a dedicated model configuration service.
    try {
        std::shared_ptr<FileHandlerImpl> file_handler = std::make_shared<FileHandlerImpl>();

        std::cout << "** Loading model configuration ** \n - Path:" << config_file << "" << "\n";
        auto file_content = file_handler->readFile(config_file);
        if (file_content.empty()) {
            throw std::runtime_error(" - Model configuration file is empty");
        }
        nlohmann::json config_json;
        config_json = nlohmann::json::parse(file_content);

        ModelConfigDTO model_config_dto = DtoService::parseModelConfigJsonToDto(config_json);
        std::cout << " - ModelConfigDTO parsed successfully\n";
        // TODO(model-config): The legacy ModelConfigConverter step is intentionally removed.
        // DtoToBo dto_to_bo(file_handler);
        // const auto model_config = dto_to_bo.convert(model_config_dto);
        const std::filesystem::path model_config_path =
            std::filesystem::absolute(std::filesystem::path(config_file));
        std::filesystem::path reasoner_profile_path(model_config_dto.reasoner_profile);
        if (reasoner_profile_path.is_relative()) {
            reasoner_profile_path = model_config_path.parent_path() / reasoner_profile_path;
        }

        const ModelConfig model_config(model_config_dto.input.signals,
                                       model_config_dto.input.rdf_shapes,
                                       reasoner_profile_path.lexically_normal().string());
        std::cout << " - Model configuration loaded successfully\n\n";
        return model_config;

    } catch (const std::exception& e) {
        throw std::runtime_error(" - Error loading model configuration: " + std::string(e.what()));
    }
}
