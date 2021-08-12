#include "gtest/gtest.h"
#include "stout/just.h"
#include "stout/libuv/loop.h"
#include "stout/libuv/timer.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Just;
using stout::eventuals::Terminate;
using stout::uv::Loop;
using stout::uv::Timer;

TEST(Libuv, SimpleTimerTest) {
  Loop loop;

  auto e = Timer(loop, 10);

  auto [future, k] = Terminate(e);

  eventuals::start(k);

  EXPECT_EQ(loop.clock().timers_active(), 1);

  uint64_t start = uv_now(loop);
  loop.run(stout::uv::Loop::DEFAULT);
  uint64_t end = uv_now(loop);
  uint64_t diff = end - start;

  EXPECT_EQ(loop.clock().timers_active(), 0);
  EXPECT_TRUE(diff > 0 && diff < 20) << diff; // check if the timeout of timer2 was indeed 10ms
}

TEST(Libuv, FooAbstraction) {
  class Foo {
   public:
    Foo(Loop& loop)
      : loop_(loop) {}

    auto Operation() {
      return Timer(loop_, 5000)
          | Just(42);
    }

   private:
    Loop& loop_;
  };

  Loop loop;

  Foo foo(loop);

  auto e = foo.Operation();
  auto [future, k] = Terminate(e);

  loop.clock().Pause();

  eventuals::start(k);

  EXPECT_EQ(loop.clock().timers_active(), 1);

  EXPECT_FALSE(uv_loop_alive(loop));

  loop.clock().Advance(1000);

  EXPECT_FALSE(uv_loop_alive(loop));

  loop.clock().Advance(4000);

  EXPECT_TRUE(uv_loop_alive(loop));

  EXPECT_EQ(loop.clock().timers_active(), 1);

  loop.run(stout::uv::Loop::ONCE);

  EXPECT_EQ(loop.clock().timers_active(), 0);

  EXPECT_EQ(42, future.get());
}

TEST(Libuv, AddTimerAfterAdvancing) {
  Loop loop;

  Loop::Clock& clock = loop.clock();

  clock.Pause();

  auto e1 = Timer(loop, 5000);
  auto [future1, k1] = Terminate(e1);
  eventuals::start(k1);

  EXPECT_EQ(loop.clock().timers_active(), 1);

  clock.Advance(1000);
  // timer1 - 4000ms

  auto e2 = Timer(loop, 5000);
  auto [future2, k2] = Terminate(e2);
  eventuals::start(k2);

  EXPECT_EQ(loop.clock().timers_active(), 2);

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Advance(4000);
  // timer1 - fired! timer2 - 1000ms

  EXPECT_TRUE(uv_loop_alive(loop));

  loop.run(stout::uv::Loop::ONCE); // fire the timer1

  EXPECT_EQ(loop.clock().timers_active(), 1);

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Advance(990);
  // timer2 - 10ms

  EXPECT_FALSE(uv_loop_alive(loop));

  clock.Resume();

  uint64_t start = uv_now(loop);
  loop.run(stout::uv::Loop::DEFAULT);
  uint64_t end = uv_now(loop);
  uint64_t diff = end - start;

  EXPECT_EQ(loop.clock().timers_active(), 0);

  EXPECT_TRUE(diff > 0 && diff < 20) << diff; // check if the timeout of timer2 was indeed 10ms
}
