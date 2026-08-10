add_library(reasoner
    ${CMAKE_CURRENT_SOURCE_DIR}/rdfox/src/rdfox_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/request_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/reasoner_profile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/services/reasoner_factory.cpp
)

target_include_directories(reasoner 
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/rdfox/src
        ${Boost_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/services
        ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
)

target_link_libraries(reasoner
    PUBLIC
        utils
        nlohmann_json::nlohmann_json
    PRIVATE 
        Boost::system 
        Boost::thread
)
