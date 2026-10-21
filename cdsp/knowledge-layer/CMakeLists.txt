cmake_minimum_required(VERSION 3.25)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(knowledge-layer VERSION 1.0 LANGUAGES CXX)

option(KL_BUILD_TESTS "Build Knowledge Layer test targets" ON)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the safety policy related to changes in handling the FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# Find Boost libraries used by the WebSocket transport and reasoner adapter.
find_package(Boost 1.87 REQUIRED COMPONENTS system)

# FetchContent to include dependencies
include(FetchContent)

# ########################
# nlohmann/json dependency
# ########################

# Prefer a system package so container builds do not require GitHub access.
# Keep the pinned FetchContent fallback for developer environments without it.
find_package(nlohmann_json QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(json)
endif()

# #####################
# Serd dependency
# #####################

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SERD QUIET IMPORTED_TARGET serd-0)
endif()

if(TARGET PkgConfig::SERD)
    set(SERD_LIBRARIES PkgConfig::SERD)
    set(SERD_INCLUDE_DIRS "")
else()
    FetchContent_Declare(
        serd
        GIT_REPOSITORY https://github.com/jtsylve/serd-cmake.git
        GIT_TAG e4ace09 # Hash of the last stable commit
    )

    # Ensure SERD_BUILD_SERDI is OFF to prevent building serdi executable.
    set(SERD_BUILD_SERDI OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(serd)

    if(TARGET serdi)
        set_target_properties(serdi PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
    endif()

    set(SERD_LIBRARIES serd)
    set(SERD_INCLUDE_DIRS "${serd_SOURCE_DIR}/include")
endif()

# #####################
# GoogleTest dependency
# #####################

if(KL_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT TARGET GTest::gtest_main)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
        )
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# ####################

# Output directory for the executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Define the project directories
set(PROJECT_ROOT_DIR "${CMAKE_SOURCE_DIR}")
set(TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/tests/outputs/")

# Pass the directories to your C++ code
add_compile_definitions(PROJECT_ROOT="${PROJECT_ROOT_DIR}")
add_compile_definitions(TEST_OUTPUT_DIR="${TEST_OUTPUT_DIR}")

# Add subdirectories for project modules and tests
add_subdirectory(connection/ws-connection)
add_subdirectory(connection/il-protocol)
add_subdirectory(rdf-mapper)
add_subdirectory(config-parser)
add_subdirectory(reasoner-engine)

if(KL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(connection/ws-connection/tests)
    add_subdirectory(connection/il-protocol/tests)
    add_subdirectory(config-parser/tests)
endif()

add_executable(driving_style_client src/main.cpp)
target_link_libraries(driving_style_client
    PRIVATE
        config-parser
        il-protocol
        ws-connection
)
