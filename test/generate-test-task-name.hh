#pragma once

#include "gtest/gtest.h"

// Helper that generates a task name based on the current test as well
// as the file and line where called to differentiate more than one
// task in the same test.
#define GenerateTestTaskName() []() {                                        \
  const ::testing::TestInfo* const test =                                    \
      ::testing::UnitTest::GetInstance()->current_test_info();               \
  CHECK_NOTNULL(test);                                                       \
  return std::string(test->name()) + "."                                     \
      + std::string(test->test_suite_name())                                 \
      + " (" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + ")"; \
}()
