#pragma once

#include "gtest/gtest.h"
#include "stout/event-loop.h"

class EventLoopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stout::eventuals::EventLoop::DefaultReset();
  }

  void TearDown() override {
    // TODO(benh): shutdown event loop so that we can ensure there
    // aren't any active threads for making sure we don't have a
    // thread/resource leak (consider using
    // 'testing::internal::GetThreadCount()' to get the number of
    // threads).
  }
};
