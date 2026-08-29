add_executable(il_message_serialization_tests serialization_test.cpp)

target_link_libraries(il_message_serialization_tests
    PRIVATE
        il_message
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(il_message_serialization_tests)
