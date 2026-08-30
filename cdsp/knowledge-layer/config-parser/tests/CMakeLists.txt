add_executable(config-parser-tests model_config_parser_test.cpp)

target_link_libraries(config-parser-tests
    PRIVATE
        GTest::gtest_main
        config-parser
)

add_test(NAME ConfigParserTests COMMAND config-parser-tests)

set_target_properties(config-parser-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
)
