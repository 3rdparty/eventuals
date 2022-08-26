#include "eventuals/executor.hh"

#include "eventuals/eventual.hh"
#include "eventuals/just.hh"
#include "eventuals/promisify.hh"
#include "eventuals/task.hh"
#include "eventuals/then.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

TEST(ExecutorTest, Succeed) {
  bool executed = false;

  Executor<Task::Of<void>> executor("executor");

  auto e = [&]() {
    return executor.Submit(
               Task::Of<void>(
                   [&]() {
                     executed = true;
                     return Just();
                   }))
        >> Then([&]() {
             return executor.Shutdown();
           })
        >> Then([&]() {
             return executor.Wait();
           });
  };

  *e();

  EXPECT_TRUE(executed);
}


TEST(ExecutorTest, Interrupt) {
  bool interrupted = false;

  Executor<Task::Of<void>> executor("executor");

  auto e = [&]() {
    return executor.Submit(
               Task::Of<void>(
                   [&]() {
                     return Eventual<void>()
                         .interruptible()
                         .start([&](auto& k, auto& handler) {
                           CHECK(handler)
                               << "Test expects interrupt to be registered";
                           handler->Install([&]() {
                             interrupted = true;
                             k.Stop();
                           });
                         });
                   }))
        >> Then([&]() {
             return executor.InterruptAndShutdown();
           })
        >> Then([&]() {
             return executor.Wait();
           });
  };

  *e();

  EXPECT_TRUE(interrupted);
}

} // namespace
} // namespace eventuals::test
