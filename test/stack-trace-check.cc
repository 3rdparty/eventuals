#include "eventuals/just.h"
#include "eventuals/then.h"
#include "glog/logging.h"
#include "gtest/gtest.h"

// For setting up gtest environment in order to use
// `glog` failure signal handler:
// https://github.com/google/glog#failure-signal-handler
#include "signal-handler-env.h"

using namespace eventuals;

static testing::Environment* const signal_handler_failure_env =
    testing::AddGlobalTestEnvironment(new SignalHandlerEnvironment{});

TEST(StackTrace, CheckFail) {
  auto e = []() {
    return Just(42)
        | Then([](int i) {
             CHECK_GE(i, 100);
             return std::to_string(i);
           });
  };

  std::string result{*e()};

  EXPECT_STREQ("42", result.c_str());
}
