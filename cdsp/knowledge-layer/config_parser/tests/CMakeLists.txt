add_executable(config_parser_tests model_config_parser_test.cpp)

target_link_libraries(config_parser_tests
    PRIVATE
        GTest::gtest_main
        config_parser
)

add_test(NAME ConfigParserTests COMMAND config_parser_tests)

set_target_properties(config_parser_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
)
