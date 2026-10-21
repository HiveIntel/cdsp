add_library(il-protocol
    src/il_message_parser.cpp
)

target_compile_features(il-protocol PUBLIC cxx_std_17)

target_include_directories(il-protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(il-protocol
    PUBLIC
        nlohmann_json::nlohmann_json
)

