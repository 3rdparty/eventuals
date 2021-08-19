#include "stout/timer.h"

#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/just.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::EventLoop;
using stout::eventuals::Just;
using stout::eventuals::Terminate;
using stout::eventuals::Timer;

TEST(TimerTest, SimpleTimer) {
  EventLoop loop;

  auto e = Timer(loop, std::chrono::milliseconds(10));

  auto [future, k] = Terminate(e);

  eventuals::start(k);

  EXPECT_EQ(loop.clock().timers_active(), 1);

  uint64_t start = uv_now(loop);
  loop.run(EventLoop::DEFAULT);
  uint64_t end = uv_now(loop);
  uint64_t diff = end - start;

  EXPECT_EQ(loop.clock().timers_active(), 0);
  EXPECT_TRUE(diff > 0 && diff < 20) << diff; // check if the timeout of timer2 was indeed 10ms
}

TEST(TimerTest, FooAbstraction) {
  class Foo {
   public:
    Foo(EventLoop& loop)
      : loop_(loop) {}

    auto Operation() {
      return Timer(loop_, std::chrono::seconds(5))
          | Just(42);
    }

   private:
    EventLoop& loop_;
  };

  EventLoop loop;

  Foo foo(loop);

  auto e = foo.Operation();
  auto [future, k] = Terminate(e);

  loop.clock().Pause();

  eventuals::start(k);

  EXPECT_EQ(loop.clock().timers_active(), 1);

  EXPECT_FALSE(uv_loop_alive(loop));

  loop.clock().Advance(std::chrono::seconds(1));

  EXPECT_FALSE(uv_loop_alive(loop));

  loop.clock().Advance(std::chrono::seconds(4));

  EXPECT_TRUE(uv_loop_alive(loop));

  EXPECT_EQ(loop.clock().timers_active(), 1);

  loop.run(EventLoop::ONCE);

  EXPECT_EQ(loop.clock().timers_active(), 0);

  EXPECT_EQ(42, future.get());
}

TEST(TimerTest, AddTimerAfterAdvancingClock) {
  EventLoop loop;

  EventLoop::Clock& clock = loop.clock();

  clock.Pause();

  auto e1 = Timer(loop, std::chrono::seconds(5));
  auto [future1, k1] = Terminate(e1);
  eventuals::start(k1);

  EXPECT_EQ(loop.clock().timers_active(), 1);

  clock.Advance(std::chrono::seconds(1));
  // timer1 - 4000ms

  auto e2 = Timer(loop, std::chrono::seconds(5));
  auto [future2, k2] = Terminate(e2);
  eventuals::start(k2);

  EXPECT_EQ(loop.clock().timers_active(), 2);

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Advance(std::chrono::seconds(4));
  // timer1 - fired! timer2 - 1000ms

  EXPECT_TRUE(uv_loop_alive(loop));

  loop.run(EventLoop::ONCE); // fire the timer1

  EXPECT_EQ(loop.clock().timers_active(), 1);

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Advance(std::chrono::milliseconds(990));
  // timer2 - 10ms

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Resume();

  uint64_t start = uv_now(loop);
  loop.run(EventLoop::DEFAULT);
  uint64_t end = uv_now(loop);
  uint64_t diff = end - start;

  EXPECT_EQ(loop.clock().timers_active(), 0);

  EXPECT_TRUE(diff > 0 && diff < 20) << diff; // check if the timeout of timer2 was indeed 10ms
}
