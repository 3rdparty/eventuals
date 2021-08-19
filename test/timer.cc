#include "stout/timer.h"

#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/just.h"
#include "stout/terminal.h"
#include "test/event-loop-test.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Clock;
using stout::eventuals::EventLoop;
using stout::eventuals::Just;
using stout::eventuals::Terminate;
using stout::eventuals::Timer;

TEST_F(EventLoopTest, Timer) {
  auto e = Timer(std::chrono::milliseconds(10));

  auto [future, k] = Terminate(e);

  eventuals::start(k);

  auto start = Clock().Now();
  EventLoop::Default().Run();
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future.get();
}


TEST_F(EventLoopTest, PauseAndAdvanceClock) {
  Clock().Pause();

  auto e = Timer(std::chrono::seconds(5))
      | Just(42);

  auto [future, k] = Terminate(e);

  eventuals::start(k);

  EXPECT_FALSE(EventLoop::Default().Alive());

  Clock().Advance(std::chrono::seconds(1));

  EXPECT_FALSE(EventLoop::Default().Alive());

  Clock().Advance(std::chrono::seconds(4));

  EXPECT_TRUE(EventLoop::Default().Alive());

  EventLoop::Default().Run();

  EXPECT_EQ(42, future.get());

  Clock().Resume();
}


TEST_F(EventLoopTest, AddTimerAfterAdvancingClock) {
  Clock().Pause();

  auto e1 = Timer(std::chrono::seconds(5));
  auto [future1, k1] = Terminate(e1);
  eventuals::start(k1);

  Clock().Advance(std::chrono::seconds(1)); // Timer 1 in 4000ms.

  auto e2 = Timer(std::chrono::seconds(5));
  auto [future2, k2] = Terminate(e2);
  eventuals::start(k2);

  EXPECT_FALSE(EventLoop::Default().Alive());

  Clock().Advance(std::chrono::seconds(4)); // Timer 1 fired, timer 2 in 1000ms.

  EXPECT_TRUE(EventLoop::Default().Alive());

  EventLoop::Default().Run(); // Fire timer 1.

  future1.get();

  EXPECT_FALSE(EventLoop::Default().Alive());

  Clock().Advance(std::chrono::milliseconds(990)); // Timer 2 in 10ms.

  EXPECT_FALSE(EventLoop::Default().Alive());

  Clock().Resume();

  auto start = Clock().Now();
  EventLoop::Default().Run();
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future2.get();
}
