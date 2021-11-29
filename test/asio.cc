#include "event-loop-test.h"
#include "eventuals/event-loop.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Clock;
using eventuals::EventLoop;
using eventuals::Terminate;
using eventuals::Then;
using eventuals::Timer;

class ASIOEventLoopTest : public EventLoopTest {};

TEST_F(ASIOEventLoopTest, ASIOTimer) {
  testing::MockFunction<void()> func;

  EXPECT_CALL(func, Call).Times(1);

  asio::system_timer timer(
      EventLoop::Default().context(),
      std::chrono::milliseconds(10));

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  EXPECT_EQ(
      future.wait_for(std::chrono::nanoseconds::zero()),
      std::future_status::timeout);

  timer.async_wait([&func, &promise](asio::error_code ec) {
    EXPECT_TRUE(!ec);
    func.Call();
    promise.set_value(true);
  });

  auto start = Clock().Now();
  EventLoop::Default().RunUntil(future);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  EXPECT_TRUE(future.get());
}

TEST_F(ASIOEventLoopTest, ASIOTimerAndEventualTimer) {
  testing::MockFunction<void()> func;

  EXPECT_CALL(func, Call).Times(2);

  asio::system_timer timer(
      EventLoop::Default().context(),
      std::chrono::milliseconds(10));

  std::promise<void> promise1;
  std::future<void> future1 = promise1.get_future();

  EXPECT_EQ(
      future1.wait_for(std::chrono::nanoseconds::zero()),
      std::future_status::timeout);

  timer.async_wait([&func, &promise1](asio::error_code ec) {
    EXPECT_TRUE(!ec);
    func.Call();
    promise1.set_value();
  });

  auto e2 = [&func]() {
    return Timer(std::chrono::milliseconds(10))
        | Then([&func]() {
             func.Call();
           });
  };

  auto [future2, k2] = Terminate(e2());

  k2.Start();

  auto start = Clock().Now();
  EventLoop::Default().RunUntil(future1);
  EventLoop::Default().RunUntil(future2);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future1.get();
  future2.get();
}
