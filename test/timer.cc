#include "eventuals/timer.h"

#include "event-loop-test.h"
#include "eventuals/event-loop.h"
#include "eventuals/just.h"
#include "eventuals/terminal.h"
#include "gtest/gtest.h"

using eventuals::Clock;
using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Just;
using eventuals::Terminate;
using eventuals::Timer;

TEST_F(EventLoopTest, Timer) {
  auto e = []() {
    return Timer(std::chrono::milliseconds(10));
  };

  auto [future, k] = Terminate(e());

  k.Start();

  auto start = Clock().Now();
  EventLoop::Default().Run();
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future.get();
}


TEST_F(EventLoopTest, PauseAndAdvanceClock) {
  Clock().Pause();

  auto e = []() {
    return Timer(std::chrono::seconds(5))
        | Just(42);
  };

  auto [future, k] = Terminate(e());

  k.Start();

  Clock().Advance(std::chrono::seconds(5));

  EventLoop::Default().Run();

  EXPECT_EQ(42, future.get());

  Clock().Resume();
}


TEST_F(EventLoopTest, AddTimerAfterAdvancingClock) {
  Clock().Pause();

  auto e1 = []() {
    return Timer(std::chrono::seconds(5));
  };

  auto [future1, k1] = Terminate(e1());

  k1.Start();

  Clock().Advance(std::chrono::seconds(1)); // Timer 1 in 4000ms.

  auto e2 = []() {
    return Timer(std::chrono::seconds(5));
  };

  auto [future2, k2] = Terminate(e2());

  k2.Start();

  Clock().Advance(std::chrono::seconds(4)); // Timer 1 fired, timer 2 in 1000ms.

  EventLoop::Default().Run(); // Fire timer 1.

  future1.get();

  Clock().Advance(std::chrono::milliseconds(990)); // Timer 2 in 10ms.

  Clock().Resume();

  auto start = Clock().Now();
  EventLoop::Default().Run();
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future2.get();
}


TEST_F(EventLoopTest, InterruptTimer) {
  auto e = []() {
    return Timer(std::chrono::seconds(100));
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  auto thread = std::thread([&]() {
    interrupt.Trigger();
  });

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  thread.join();
}


TEST_F(EventLoopTest, PauseClockInterruptTimer) {
  Clock().Pause();

  auto e = []() {
    return Timer(std::chrono::seconds(100));
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_F(EventLoopTest, TimerAfterTimer) {
  auto e = []() {
    return Timer(std::chrono::milliseconds(5))
        | Timer(std::chrono::milliseconds(5));
  };

  auto [future, k] = Terminate(e());

  k.Start();

  auto start = Clock().Now();
  EventLoop::Default().Run();
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future.get();
}
