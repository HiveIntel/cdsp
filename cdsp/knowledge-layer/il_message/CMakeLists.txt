add_library(il_message
    src/get_request.cpp
    src/inbound_message_parser.cpp
    src/node.cpp
    src/request.cpp
    src/set_request.cpp
    src/subscribe_request.cpp
    src/unsubscribe_request.cpp
)

target_include_directories(il_message
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
