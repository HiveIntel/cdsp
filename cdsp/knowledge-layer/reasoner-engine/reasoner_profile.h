#ifndef REASONER_PROFILE_H
#define REASONER_PROFILE_H

#include <filesystem>
#include <string>
#include <vector>

// Normalized configuration loaded from a reasoner-profile JSON file.
struct ReasonerProfile {
    struct Endpoint {
        std::string host = "127.0.0.1";
        unsigned short port = 12110;
        std::string datastore = "driving-style";
    };

    std::string name;
    std::vector<std::filesystem::path> ontologies;
    std::vector<std::filesystem::path> rules;
    std::vector<std::filesystem::path> queries;
    Endpoint endpoint;
};

class ReasonerProfileParser {
   public:
    // Relative resource paths are resolved against the profile file's directory.
    static ReasonerProfile parse(const std::filesystem::path& profile_file_path);
};

#endif  // REASONER_PROFILE_H
