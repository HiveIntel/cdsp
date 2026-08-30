add_library(reasoner_engine
    rdf4j_reasoner_service.cpp
    reasoner_factory.cpp
    reasoner_profile.cpp
)

target_include_directories(reasoner_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(reasoner_engine
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        Boost::system
)
