#ifndef REASONER_PROFILE_H
#define REASONER_PROFILE_H

#include <filesystem>
#include <string>
#include <vector>

// Normalized configuration loaded from a reasoner-profile JSON file.
struct ReasonerProfile {
    std::string name;
    std::vector<std::filesystem::path> ontologies;
    std::vector<std::filesystem::path> rules;
    std::vector<std::filesystem::path> queries;
};

class ReasonerProfileParser {
   public:
    // Relative resource paths are resolved against the profile file's directory.
    static ReasonerProfile parse(const std::filesystem::path& profile_file_path);
};

#endif  // REASONER_PROFILE_H
