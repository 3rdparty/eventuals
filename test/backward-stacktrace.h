#pragma once

#include "backward.hpp"
#include "gtest/gtest.h"

namespace backward_stacktrace {

// Do set-up and tear-down at the test program level.
// When RUN_ALL_TESTS() is called, it first calls the
// SetUp() method of the environment object, then runs
// the tests if there was no fatal failures, and finally
// calls TearDown() of the environment object. If there
// was any failure - a simple helper class
// `backward::SignalHandling` will register this for us
// and print out consistent stacktrace.
// (check_line_length skip)
// https://github.com/YOU-i-Labs/googletest/blob/master/googletest/docs/V1_7_AdvancedGuide.md#global-set-up-and-tear-down
class BackwardStackTrace : public ::testing::Environment {
 private:
  // A simple helper class that registers for you the most
  // common signals and other callbacks to segfault,
  // hardware exception, un-handled exception etc.
  backward::SignalHandling sh;
};

} // namespace backward_stacktrace
