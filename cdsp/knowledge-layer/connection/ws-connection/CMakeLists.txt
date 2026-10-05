add_library(ws-connection
    src/websocket_client.cpp
)

target_compile_features(ws-connection PUBLIC cxx_std_17)

target_include_directories(ws-connection
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ws-connection
    PRIVATE
        Boost::system
)

if(WIN32)
    target_link_libraries(ws-connection PRIVATE ws2_32)
endif()
