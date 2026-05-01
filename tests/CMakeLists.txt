# Cucumber-cpp BDD acceptance tests for C++ poker examples
#
# This module sets up cucumber-cpp with GTest backend to run
# acceptance tests defined in Gherkin feature files.

include(FetchContent)

# Find GTest (system-installed, required for cucumber-cpp)
find_package(GTest 1.11.0 REQUIRED)

# Find Boost (required by cucumber-cpp for regex).
# CMP0167 was added in CMake 3.30; angzarr-cpp:latest ships older CMake,
# so guard against the unknown-policy error.
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost 1.70 CONFIG QUIET COMPONENTS regex filesystem system program_options thread)
if(NOT Boost_FOUND)
    find_package(Boost 1.70 REQUIRED COMPONENTS regex filesystem system program_options thread)
endif()

# Find nlohmann_json (required by cucumber-cpp)
find_package(nlohmann_json 3.10 REQUIRED)

# Set cucumber-cpp options before FetchContent
set(CUKE_ENABLE_QT OFF CACHE BOOL "Disable Qt support in cucumber-cpp")
set(CUKE_TESTS_UNIT OFF CACHE BOOL "Disable cucumber-cpp unit tests")
set(CUKE_TESTS_E2E OFF CACHE BOOL "Disable cucumber-cpp e2e tests")
set(CUKE_ENABLE_GTEST ON CACHE BOOL "Enable GTest backend")
set(CUKE_ENABLE_BOOST_TEST OFF CACHE BOOL "Disable Boost.Test backend")
set(CUKE_ENABLE_EXAMPLES OFF CACHE BOOL "Disable cucumber-cpp examples")

# Disable cucumber-cpp install targets to avoid gtest export conflicts
set(CMAKE_SKIP_INSTALL_RULES ON)

# Disable warnings-as-errors for dependencies to avoid GCC 12+ false positives
# with gtest internal headers (char_traits.h)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=restrict")

# Fetch cucumber-cpp
FetchContent_Declare(
    cucumber-cpp
    GIT_REPOSITORY https://github.com/cucumber/cucumber-cpp.git
    GIT_TAG        v0.7.0
)
FetchContent_MakeAvailable(cucumber-cpp)

# Handler sources from aggregates (compiled into tests since no library targets exist)
set(PLAYER_HANDLER_SOURCES
    ${CMAKE_SOURCE_DIR}/player/agg/src/player_state.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/register_handler.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/deposit_handler.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/withdraw_handler.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/reserve_handler.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/release_handler.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/transfer_handler.cpp
    ${CMAKE_SOURCE_DIR}/player/agg/handlers/rejected_handler.cpp
)

set(TABLE_HANDLER_SOURCES
    ${CMAKE_SOURCE_DIR}/table/agg/src/table_state.cpp
    ${CMAKE_SOURCE_DIR}/table/agg/handlers/create_handler.cpp
    ${CMAKE_SOURCE_DIR}/table/agg/handlers/join_handler.cpp
    ${CMAKE_SOURCE_DIR}/table/agg/handlers/leave_handler.cpp
    ${CMAKE_SOURCE_DIR}/table/agg/handlers/start_hand_handler.cpp
    ${CMAKE_SOURCE_DIR}/table/agg/handlers/end_hand_handler.cpp
)

set(HAND_HANDLER_SOURCES
    ${CMAKE_SOURCE_DIR}/hand/agg/src/hand_state.cpp
    ${CMAKE_SOURCE_DIR}/hand/agg/handlers/deal_handler.cpp
    ${CMAKE_SOURCE_DIR}/hand/agg/handlers/post_blind_handler.cpp
    ${CMAKE_SOURCE_DIR}/hand/agg/handlers/action_handler.cpp
    ${CMAKE_SOURCE_DIR}/hand/agg/handlers/deal_community_handler.cpp
    ${CMAKE_SOURCE_DIR}/hand/agg/handlers/award_pot_handler.cpp
)

# Common include directories for test executables
set(TEST_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/player/agg/src
    ${CMAKE_SOURCE_DIR}/player/agg/handlers
    ${CMAKE_SOURCE_DIR}/table/agg/src
    ${CMAKE_SOURCE_DIR}/table/agg/handlers
    ${CMAKE_SOURCE_DIR}/hand/agg/src
    ${CMAKE_SOURCE_DIR}/hand/agg/handlers
)

# Common link libraries for test executables
set(TEST_LINK_LIBS
    cucumber-cpp
    GTest::gtest
    GTest::gtest_main
    Boost::regex
    Boost::filesystem
    Boost::system
    Boost::program_options
    Boost::thread
    nlohmann_json::nlohmann_json
    angzarr_client
    angzarr_proto
)

# ===========================================================================
# Unit Tests - in-process handler calls via cucumber-cpp wire protocol
# ===========================================================================

# Step definition sources - compiled as single unit to avoid __COUNTER__ collisions
# between translation units (cucumber-cpp uses __COUNTER__ for unique class names)
set(UNIT_STEP_SOURCES
    all_steps.cpp
)

add_executable(unit_tests
    runner.cpp
    ${UNIT_STEP_SOURCES}
    ${PLAYER_HANDLER_SOURCES}
    ${TABLE_HANDLER_SOURCES}
    ${HAND_HANDLER_SOURCES}
)

target_include_directories(unit_tests PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(unit_tests PRIVATE ${TEST_LINK_LIBS})

# Disable -Werror=restrict for gtest headers which trigger false positives
# in gtest type-util.h with GCC 12+ and certain optimization levels
target_compile_options(unit_tests PRIVATE -Wno-error=restrict)

# Also build legacy target name for backward compatibility
add_executable(acceptance_tests
    runner.cpp
    ${UNIT_STEP_SOURCES}
    ${PLAYER_HANDLER_SOURCES}
    ${TABLE_HANDLER_SOURCES}
    ${HAND_HANDLER_SOURCES}
)

target_include_directories(acceptance_tests PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(acceptance_tests PRIVATE ${TEST_LINK_LIBS})
target_compile_options(acceptance_tests PRIVATE -Wno-error=restrict)

# ===========================================================================
# Gherkin Acceptance Tests - CommandClient abstraction (in-process or gRPC)
# ===========================================================================

# Acceptance step definitions use CommandClient interface
set(ACCEPTANCE_STEP_SOURCES
    all_acceptance_steps.cpp
    command_client.cpp
)

add_executable(acceptance_runner
    acceptance_runner.cpp
    ${ACCEPTANCE_STEP_SOURCES}
    ${PLAYER_HANDLER_SOURCES}
    ${TABLE_HANDLER_SOURCES}
    ${HAND_HANDLER_SOURCES}
)

target_include_directories(acceptance_runner PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(acceptance_runner PRIVATE ${TEST_LINK_LIBS})
target_compile_options(acceptance_runner PRIVATE -Wno-error=restrict)

# Feature files location
set(FEATURE_DIR "${CMAKE_SOURCE_DIR}/features")
set(UNIT_FEATURE_DIR "${FEATURE_DIR}/unit")
set(ACCEPTANCE_FEATURE_DIR "${FEATURE_DIR}/acceptance")

# ===========================================================================
# CTest integration
# ===========================================================================

# Unit tests (label: unit) - in-process handler calls
add_test(
    NAME unit_tests
    COMMAND unit_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(unit_tests PROPERTIES
    LABELS "unit"
    ENVIRONMENT "CUCUMBER_FEATURES_DIR=${UNIT_FEATURE_DIR}"
)

# Legacy acceptance_tests target (label: acceptance) - same as unit_tests
add_test(
    NAME acceptance_tests
    COMMAND acceptance_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(acceptance_tests PROPERTIES
    LABELS "acceptance"
    ENVIRONMENT "CUCUMBER_FEATURES_DIR=${FEATURE_DIR}"
)

# Gherkin acceptance tests (label: gherkin-acceptance)
# Uses CommandClient: PLAYER_URL -> gRPC, otherwise in-process
add_test(
    NAME gherkin_acceptance
    COMMAND acceptance_runner
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(gherkin_acceptance PROPERTIES
    LABELS "gherkin-acceptance"
    ENVIRONMENT "CUCUMBER_FEATURES_DIR=${ACCEPTANCE_FEATURE_DIR}"
)

# ===========================================================================
# Custom targets
# ===========================================================================

# Run unit tests
add_custom_target(test-unit
    COMMAND ${CMAKE_CTEST_COMMAND} -L unit --output-on-failure
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running unit tests (in-process handlers)"
)

# Run acceptance tests via CommandClient
add_custom_target(test-acceptance
    COMMAND ${CMAKE_CTEST_COMMAND} -L gherkin-acceptance --output-on-failure
    DEPENDS acceptance_runner
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Gherkin acceptance tests via CommandClient"
)
