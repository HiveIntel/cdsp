add_executable(il-request-serialization-tests serialization_test.cpp)

target_link_libraries(il-request-serialization-tests
    PRIVATE
        il-request
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(il-request-serialization-tests)
