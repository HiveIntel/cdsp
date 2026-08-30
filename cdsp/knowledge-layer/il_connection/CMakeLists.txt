add_library(il_connection
    src/real_websocket_connection.cpp
    src/il_client.cpp
)

target_include_directories(il_connection
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(il_connection
    PUBLIC
        nlohmann_json::nlohmann_json
        il_message
        rdf_mapper
        reasoner_engine
    PRIVATE
        Boost::system
        Boost::filesystem
        Boost::thread
)

if(WIN32)
    target_link_libraries(il_connection PRIVATE ws2_32)
endif()
