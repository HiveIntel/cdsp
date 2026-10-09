add_library(rdf-mapper
    src/rdf_mapper.cpp
    src/rdf_model.cpp
    src/rdf_writer.cpp
    src/serd_rdf_reader.cpp
    src/shacl_parser.cpp
)

target_include_directories(rdf-mapper
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${SERD_INCLUDE_DIRS}
)

target_link_libraries(rdf-mapper
    PUBLIC
        il-protocol
    PRIVATE
        nlohmann_json::nlohmann_json
        ${SERD_LIBRARIES}
)

include(CTest)

if(KL_BUILD_TESTS)
    add_executable(rdf-mapper-tests
        tests/property_shape_parser_test.cpp
        tests/rdf_mapper_parser_integration_test.cpp
        tests/rdf_mapper_signal_test.cpp
        tests/rdf_graph_test.cpp
        tests/rdf_model_test.cpp
        tests/serd_rdf_reader_test.cpp
    )

    target_link_libraries(rdf-mapper-tests
        PRIVATE
            GTest::gtest_main
            rdf-mapper
    )

    target_compile_definitions(rdf-mapper-tests
        PRIVATE
            RDF_MAPPER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )

    add_test(NAME rdf-mapper-tests COMMAND rdf-mapper-tests)
endif()
