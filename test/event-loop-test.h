#pragma once

#include "gtest/gtest.h"
#include "stout/event-loop.h"

class EventLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stout::eventuals::EventLoop::ConstructDefault();
  }

  void TearDown() override {
    stout::eventuals::EventLoop::DestructDefault();
  }
};
