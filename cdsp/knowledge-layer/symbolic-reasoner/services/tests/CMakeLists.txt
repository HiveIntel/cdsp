# Add profile, factory, and pull/push service tests.
add_executable(reasoner_factory_integration_tests reasoner_factory_integration_test.cpp)
target_link_libraries(reasoner_factory_integration_tests 
    PRIVATE 
        GTest::gtest_main 
        reasoner
)

# Add unit and integration tests to CTest
add_test(NAME ReasonerFactoryIntegrationTests COMMAND reasoner_factory_integration_tests)

# Define custom output directory for test binaries
set_target_properties(reasoner_factory_integration_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests")

# Ensure tests are built with the all target
add_custom_target(symbolic_reasoner_service_test ALL DEPENDS reasoner_factory_integration_tests)
