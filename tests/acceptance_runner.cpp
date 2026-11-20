/// Cucumber-cpp test runner for acceptance tests.
///
/// This is the entry point for running Gherkin acceptance tests against
/// either the in-process handlers or a deployed gRPC coordinator.
///
/// The CommandClient is initialized based on PLAYER_URL env var:
///   set   -> GrpcClient (connects to deployed coordinator)
///   unset -> InProcessClient (calls handlers directly)

// GTest must be included before cucumber-cpp autodetect for framework detection
#include <gtest/gtest.h>
#include <cucumber-cpp/autodetect.hpp>

#include "command_client.hpp"

// The autodetect header handles everything - it provides a main()
// that sets up the cucumber test infrastructure and runs the features.

// Feature files are located in features/acceptance/
// and are passed to cucumber via command line or environment variable.
