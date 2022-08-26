#pragma once

#include "eventuals/event-loop.hh"
#include "gtest/gtest.h"

namespace eventuals::test {

class EventLoopTest : public ::testing::Test {
 protected:
  // NOTE: taking a 'std::function' here instead of a 'Callback'
  // because this is for testing where we don't care about dynamic
  // memory allocation and it simplifies the tests.
  void RunUntil(const std::function<bool()>& condition) {
    while (!condition()) {
      EventLoop::Default().RunUntilIdle();
    }
  }

  template <typename T>
  void RunUntil(const std::future<T>& future) {
    return RunUntil([&future]() {
      auto status = future.wait_for(std::chrono::nanoseconds::zero());
      return status == std::future_status::ready;
    });
  }

  void RunUntilIdle() {
    EventLoop::Default().RunUntilIdle();
  }

  void SetUp() override {
    EventLoop::ConstructDefault();
  }

  void TearDown() override {
    EventLoop::DestructDefault();
  }
};

} // namespace eventuals::test
