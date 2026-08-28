add_library(rdf_mapper
    rdf_generator.cpp
    rdf_mapper.cpp
    rdf_model.cpp
    rdf_writer.cpp
    shacl_shape_parser.cpp
)

target_include_directories(rdf_mapper
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${serd_SOURCE_DIR}/include
)

target_link_libraries(rdf_mapper
    PRIVATE
        nlohmann_json::nlohmann_json
        ${SERD_LIBRARIES}
)
