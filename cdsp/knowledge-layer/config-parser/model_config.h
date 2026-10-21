#ifndef CONFIG_PARSER_MODEL_CONFIG_H
#define CONFIG_PARSER_MODEL_CONFIG_H

#include <map>
#include <string>
#include <vector>

class ModelConfig {
   public:
    ModelConfig(std::vector<std::string> signals,
                std::map<std::string, std::string> rdf_shapes,
                std::string reasoner_profile);

    std::vector<std::string> getSignals() const;
    std::map<std::string, std::string> getRdfShapes() const;
    std::string getReasonerProfile() const;

   private:
    std::vector<std::string> signals_;
    std::map<std::string, std::string> rdf_shapes_;
    std::string reasoner_profile_;
};

#endif  // CONFIG_PARSER_MODEL_CONFIG_H
