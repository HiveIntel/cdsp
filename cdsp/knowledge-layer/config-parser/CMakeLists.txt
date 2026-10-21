add_library(config-parser
    model_config.cpp
    model_config_parser.cpp
)

target_include_directories(config-parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(config-parser
    PUBLIC
        nlohmann_json::nlohmann_json
)
