find_package(Threads REQUIRED)

add_executable(ws-connection-tests
    websocket_client_test.cpp
)

target_link_libraries(ws-connection-tests
    PRIVATE
        ws-connection
        GTest::gtest_main
        Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(
    ws-connection-tests
    TEST_PREFIX "ws-connection."
    PROPERTIES TIMEOUT 30
)
