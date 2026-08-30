add_executable(il_request_serialization_tests serialization_test.cpp)

target_link_libraries(il_request_serialization_tests
    PRIVATE
        il_request
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(il_request_serialization_tests)
