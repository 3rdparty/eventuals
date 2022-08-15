#pragma once

#include <chrono>
#include <functional>
#include <future>

#include "eventuals/event-loop.h"
#include "eventuals/grpc/completion-thread-pool.h"
#include "gtest/gtest.h"

namespace eventuals::grpc::test {

// 'OrderingDependentTest' provides a testing fixture that let's you
// write tests where you are about orderings between the event loop
// and gRPC.
//
// NOTE: we're explicitly not inheriting from 'EventLoopTest' to get
// things like their 'SetUp' and 'TearDown' because if functionality
// gets added to that test it might get used but not account properly
// for the gRPC thread pools as well.
class OrderingDependentTest : public ::testing::Test {
 protected:
  // NOTE: taking a 'std::function' here instead of a 'Callback'
  // because this is for testing where we don't care about dynamic
  // memory allocation and it simplifies the tests.
  void RunUntil(const std::function<bool()>& condition) {
    while (!condition()) {
      RunUntilIdle();
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
    CHECK(Clock().Paused()) << "clock is not paused!";
    CHECK(paused_thread_pools_) << "thread pools are not paused!";

    // NOTE: to break the cycle of knowing when we're really idle we
    // always run the event loop and then if running the pool(s) does
    // not run anything then we know that the event loop is also idle.
    bool possibly_added_more_work = false;
    do {
      possibly_added_more_work = false;
      EventLoop::Default().RunUntilIdle();
      for (stout::Borrowable<TestingCompletionThreadPool>& pool : pools_) {
        if (pool->RunUntilIdle()) {
          possibly_added_more_work = true;
        }
      }
    } while (possibly_added_more_work);
  }

  void PauseClockAndThreadPools() {
    CHECK(!Clock().Paused()) << "clock is already paused!";
    Clock().Pause();
    PauseThreadPools();
  }

  void ResumeClockAndThreadPools() {
    CHECK(Clock().Paused()) << "clock is not paused!";
    Clock().Resume();
    ResumeThreadPools();
  }

  void PauseThreadPools() {
    CHECK(!paused_thread_pools_) << "thread pools are already paused!";
    paused_thread_pools_ = true;
    for (stout::Borrowable<TestingCompletionThreadPool>& pool : pools_) {
      pool->Pause();
    }
  }

  void ResumeThreadPools() {
    CHECK(paused_thread_pools_) << "thread pools are not paused!";
    paused_thread_pools_ = false;
    for (stout::Borrowable<TestingCompletionThreadPool>& pool : pools_) {
      pool->Resume();
    }
  }

  void SetUp() override {
    EventLoop::ConstructDefault();
  }

  void TearDown() override {
    CHECK(!Clock().Paused()) << "you forgot to resume the clock!";
    CHECK(!paused_thread_pools_) << "you forgot to resume the thread pools!";
    pools_.clear();
    EventLoop::DestructDefault();
  }

  stout::borrowed_ref<TestingCompletionThreadPool>
  CreateTestingCompletionThreadPool() {
    auto pool = pools_.emplace_back().Borrow();
    if (paused_thread_pools_) {
      pool->Pause();
    }
    return pool;
  }

 private:
  std::deque<stout::Borrowable<TestingCompletionThreadPool>> pools_;
  bool paused_thread_pools_ = false;
};

} // namespace eventuals::grpc::test
