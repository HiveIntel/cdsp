#ifndef CONFIG_PARSER_MODEL_CONFIG_PARSER_H
#define CONFIG_PARSER_MODEL_CONFIG_PARSER_H

#include <filesystem>

#include "model_config.h"

class ModelConfigParser {
   public:
    static ModelConfig parseFile(const std::filesystem::path& config_file);
};

#endif  // CONFIG_PARSER_MODEL_CONFIG_PARSER_H
