add_library(il-request
    src/get_request.cpp
    src/inbound_message_parser.cpp
    src/node.cpp
    src/request.cpp
    src/set_request.cpp
    src/subscribe_request.cpp
    src/unsubscribe_request.cpp
)

target_include_directories(il-request
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(il-request
    PUBLIC
        nlohmann_json::nlohmann_json
)
