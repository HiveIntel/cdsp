#ifndef REASONER_FACTORY_H
#define REASONER_FACTORY_H

#include <filesystem>
#include <memory>

#include "reasoner_service.h"

class ReasonerFactory {
   public:
    // Parses the profile and returns the matching initialized reasoner service.
    static std::shared_ptr<ReasonerService> initReasoner(
        const std::filesystem::path& reasoner_profile_path);
};

#endif  // REASONER_FACTORY_H
