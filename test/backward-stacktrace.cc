#include "backward-stacktrace.h"

#include "gtest/gtest.h"

// For setting up gtest environment in order to
// provide our own stack traces for both LOG(FATAL)
// and an arbitrary signal being raised.
static testing::Environment* const kBackwardEnv =
    testing::AddGlobalTestEnvironment(
        new backward_stacktrace::BackwardStackTrace{});
