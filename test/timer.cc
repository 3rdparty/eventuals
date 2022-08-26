#include "eventuals/timer.hh"

#include "event-loop-test.hh"
#include "eventuals/event-loop.hh"
#include "eventuals/foreach.hh"
#include "eventuals/just.hh"
#include "eventuals/loop.hh"
#include "eventuals/range.hh"
#include "eventuals/repeat.hh"
#include "eventuals/take.hh"
#include "eventuals/terminal.hh"
#include "gtest/gtest.h"
#include "test/promisify-for-test.hh"

namespace eventuals::test {
namespace {

TEST_F(EventLoopTest, Timer) {
  auto e = []() {
    return Timer(std::chrono::milliseconds(10));
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  auto start = Clock().Now();
  RunUntil(future);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future.get();
}


TEST_F(EventLoopTest, PauseAndAdvanceClock) {
  Clock().Pause();

  auto e = []() {
    return Timer(std::chrono::seconds(5))
        >> Just(42);
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  Clock().Advance(std::chrono::seconds(5));

  RunUntil(future);

  EXPECT_EQ(42, future.get());

  Clock().Resume();
}


TEST_F(EventLoopTest, AddTimerAfterAdvancingClock) {
  Clock().Pause();

  auto e1 = []() {
    return Timer(std::chrono::seconds(5));
  };

  auto [future1, k1] = PromisifyForTest(e1());

  k1.Start();

  Clock().Advance(std::chrono::seconds(1)); // Timer 1 in 4000ms.

  auto e2 = []() {
    return Timer(std::chrono::seconds(5));
  };

  auto [future2, k2] = PromisifyForTest(e2());

  k2.Start();

  Clock().Advance(std::chrono::seconds(4)); // Timer 1 fired, timer 2 in 1000ms.

  RunUntil(future1); // Fire timer 1.

  future1.get();

  Clock().Advance(std::chrono::milliseconds(990)); // Timer 2 in 10ms.

  Clock().Resume();

  auto start = Clock().Now();
  RunUntil(future2);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);

  future2.get();
}


TEST_F(EventLoopTest, InterruptTimer) {
  auto e = []() {
    return Timer(std::chrono::seconds(100));
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  std::thread thread([&]() {
    interrupt.Trigger();
  });

  RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  thread.join();
}


TEST_F(EventLoopTest, PauseClockInterruptTimer) {
  Clock().Pause();

  auto e = []() {
    return Timer(std::chrono::seconds(100));
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  RunUntil(future);

  EXPECT_THROW(future.get(), eventuals::StoppedException);

  // Advance the clock so that we relinquish the borrow on the timer
  // and it can be destructed.
  Clock().Advance(std::chrono::seconds(100));

  RunUntilIdle();

  Clock().Resume();
}


TEST_F(EventLoopTest, TimerAfterTimer) {
  auto e = []() {
    return Timer(std::chrono::milliseconds(5))
        >> Timer(std::chrono::milliseconds(5));
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  auto start = Clock().Now();
  RunUntil(future);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);
}


TEST_F(EventLoopTest, MapTimer) {
  auto e = []() {
    return Foreach(
        Range(10),
        [](int) {
          return Timer(std::chrono::milliseconds(1));
        });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  auto start = Clock().Now();
  RunUntil(future);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(10), end - start);
}


TEST_F(EventLoopTest, TimerInLoopWithRegisteredInterrupt) {
  auto e = []() {
    return Repeat()
        >> Map([]() {
             return Timer(std::chrono::milliseconds(1));
           })
        >> TakeFirst(2)
        >> Loop();
  };

  auto [future, k] = PromisifyForTest(e());

  // NOTE: this test is registering an interrupt but NOT triggering it
  // to test that timers in loops with interrupts registered can
  // safely be destructed.
  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  auto start = Clock().Now();
  RunUntil(future);
  auto end = Clock().Now();

  EXPECT_LE(std::chrono::milliseconds(2), end - start);
}

} // namespace
} // namespace eventuals::test
