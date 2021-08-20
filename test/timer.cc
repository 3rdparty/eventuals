#include "stout/timer.h"

#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/just.h"
#include "stout/terminal.h"
#include "test/event-loop-test.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Clock;
using stout::eventuals::EventLoop;
using stout::eventuals::Interrupt;
using stout::eventuals::Just;
using stout::eventuals::Terminate;
using stout::eventuals::Timer;

TEST_F(EventLoopTest, Timer) {
  auto e = []() {
    return Timer(std::chrono::milliseconds(10));
  };

  auto [future, k] = Terminate(e());

  eventuals::start(k);

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

  eventuals::start(k);

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

  eventuals::start(k1);

  Clock().Advance(std::chrono::seconds(1)); // Timer 1 in 4000ms.

  auto e2 = []() {
    return Timer(std::chrono::seconds(5));
  };

  auto [future2, k2] = Terminate(e2());

  eventuals::start(k2);

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

  eventuals::start(k);

  auto thread = std::thread([&]() {
    interrupt.Trigger();
  });

  thread.detach();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST_F(EventLoopTest, PauseClockInterruptTimer) {
  Clock().Pause();

  auto e = []() {
    return Timer(std::chrono::seconds(100));
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  eventuals::start(k);

  interrupt.Trigger();

  EventLoop::Default().Run();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
