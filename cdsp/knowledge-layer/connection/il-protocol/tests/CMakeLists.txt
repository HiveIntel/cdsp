cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(il-protocol-tests LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()

    include(FetchContent)

    find_package(nlohmann_json QUIET)
    if(NOT TARGET nlohmann_json::nlohmann_json)
        FetchContent_Declare(
            json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
        )
        FetchContent_MakeAvailable(json)
    endif()

    find_package(GTest QUIET)
    if(NOT TARGET GTest::gtest_main)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()
    add_subdirectory(.. "${CMAKE_CURRENT_BINARY_DIR}/il-protocol")
endif()

find_package(Threads REQUIRED)

add_executable(il-protocol-tests
    il_protocol_test.cpp
)

target_link_libraries(il-protocol-tests
    PRIVATE
        il-protocol
        GTest::gtest_main
        Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(
    il-protocol-tests
    TEST_PREFIX "il-protocol."
    PROPERTIES TIMEOUT 30
)
