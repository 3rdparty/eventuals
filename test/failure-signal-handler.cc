#include "failure-signal-handler.h"

// For setting up gtest environment in order to use
// `glog` failure signal handler:
// https://github.com/google/glog#failure-signal-handler
static testing::Environment* const signal_handler_failure_env =
    testing::AddGlobalTestEnvironment(
        new InstallFailureSignalHandlerEnvironment{});
