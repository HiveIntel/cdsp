#ifndef SYSTEM_CONFIGURATION_SERVICE_H
#define SYSTEM_CONFIGURATION_SERVICE_H

#include <optional>
#include <string>

#include "data_types.h"
#include "model_config.h"  // TODO(system-configuration): Keep until model configuration moves to its own service.

class SystemConfigurationService {
   public:
    static SystemConfig loadSystemConfig(
        const std::optional<std::string> ws_server_host,
        const std::optional<std::string> ws_server_port,
        const std::optional<std::string> ws_server_target
    );
    // TODO(system-configuration): Reasoner configuration belongs in a dedicated reasoner service.
    // const std::optional<std::string> reasoner_server_host,
    // const std::optional<std::string> reasoner_server_port,
    // const std::optional<std::string> reasoner_server_auth_base64,
    // const std::optional<std::string> reasoner_server_data_store_name,
    // const std::optional<std::string>& reasoner_server_origin_system);
    // TODO(system-configuration): Move this API to a dedicated model configuration service.
    static ModelConfig loadModelConfig(const std::string& config_file);
};

#endif  // SYSTEM_CONFIGURATION_SERVICE_H
