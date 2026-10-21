add_library(rdf-mapper
    src/rdf_mapper.cpp
    src/rdf_model.cpp
    src/serd_rdf_writer.cpp
    src/serd_rdf_reader.cpp
    src/shacl_parser.cpp
)

target_include_directories(rdf-mapper
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${SERD_INCLUDE_DIRS}
)

target_compile_features(rdf-mapper PUBLIC cxx_std_17)

target_link_libraries(rdf-mapper
    PUBLIC
        il-protocol
    PRIVATE
        nlohmann_json::nlohmann_json
        ${SERD_LIBRARIES}
)

include(CTest)

if(KL_BUILD_TESTS)
    # Register the tests actually present in this package, including writer round trips.
    add_executable(rdf-mapper-tests
        tests/rdf_mapper_constructor_test.cpp
        tests/rdf_mapper_signal_test.cpp
        tests/serd_rdf_writer_test.cpp
    )

    # The test-only format reader uses Serd directly; the public reader stays Turtle-only.
    target_include_directories(rdf-mapper-tests PRIVATE ${SERD_INCLUDE_DIRS})

    target_link_libraries(rdf-mapper-tests
        PRIVATE
            GTest::gtest_main
            rdf-mapper
            ${SERD_LIBRARIES}
    )

    target_compile_definitions(rdf-mapper-tests
        PRIVATE
            RDF_MAPPER_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )

    include(GoogleTest)
    gtest_discover_tests(rdf-mapper-tests TEST_PREFIX "rdf-mapper." PROPERTIES TIMEOUT 30)
endif()
