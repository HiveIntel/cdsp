add_library(il-connection
    src/il_message_parser.cpp
)

target_compile_features(il-connection PUBLIC cxx_std_17)

target_include_directories(il-connection
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(il-connection
    PUBLIC
        nlohmann_json::nlohmann_json
)


