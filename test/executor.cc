#include "eventuals/executor.h"

#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
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
        | Then([&]() {
             return executor.Shutdown();
           })
        | Then([&]() {
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
                         .start([&](auto& k, Interrupt::Handler& handler) {
                           handler.Install([&]() {
                             interrupted = true;
                             k.Stop();
                           });
                         });
                   }))
        | Then([&]() {
             return executor.InterruptAndShutdown();
           })
        | Then([&]() {
             return executor.Wait();
           });
  };

  *e();

  EXPECT_TRUE(interrupted);
}

} // namespace
} // namespace eventuals::test
