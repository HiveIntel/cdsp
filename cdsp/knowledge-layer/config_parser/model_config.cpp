#include "model_config.h"

#include <stdexcept>
#include <utility>

ModelConfig::ModelConfig(std::vector<std::string> signals,
                         std::map<std::string, std::string> rdf_shapes,
                         std::string reasoner_profile)
    : signals_(std::move(signals)),
      rdf_shapes_(std::move(rdf_shapes)),
      reasoner_profile_(std::move(reasoner_profile)) {
    if (signals_.empty()) {
        throw std::invalid_argument("Model configuration signals cannot be empty.");
    }
    if (rdf_shapes_.empty()) {
        throw std::invalid_argument("Model configuration RDF shapes cannot be empty.");
    }
    if (reasoner_profile_.empty()) {
        throw std::invalid_argument("Model configuration reasoner profile cannot be empty.");
    }
}

std::vector<std::string> ModelConfig::getSignals() const { return signals_; }

std::map<std::string, std::string> ModelConfig::getRdfShapes() const { return rdf_shapes_; }

std::string ModelConfig::getReasonerProfile() const { return reasoner_profile_; }
