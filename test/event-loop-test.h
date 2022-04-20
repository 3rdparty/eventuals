#pragma once

#include "eventuals/event-loop.h"
#include "gtest/gtest.h"

namespace eventuals {
class EventLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    eventuals::EventLoop::ConstructDefault();
  }

  void TearDown() override {
    eventuals::EventLoop::DestructDefault();
  }
};
} // namespace eventuals
