#include "stout/stream.h"

#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/context.h"
#include "stout/head.h"
#include "stout/loop.h"
#include "stout/map.h"
#include "stout/reduce.h"
#include "stout/terminal.h"
#include "stout/then.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Context;
using stout::eventuals::Eventual;
using stout::eventuals::Head;
using stout::eventuals::Interrupt;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Reduce;
using stout::eventuals::Stream;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using testing::MockFunction;

TEST(StreamTest, Succeed) {
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop, done;

  EXPECT_CALL(fail, Call())
      .Times(0);

  EXPECT_CALL(stop, Call())
      .Times(0);

  EXPECT_CALL(done, Call())
      .Times(0);

  auto s = [&]() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([&](auto&, auto&) {
                 done.Call();
               })
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              })
              .fail([&](auto&, auto&, auto&&) {
                fail.Call();
              })
              .stop([&](auto&, auto&) {
                stop.Call();
              });
  };

  EXPECT_EQ(15, *s());
}


TEST(StreamTest, Done) {
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
      .Times(0);

  EXPECT_CALL(stop, Call())
      .Times(0);

  auto s = [&]() {
    return Stream<int>()
               .context(0)
               .next([](auto& value, auto& k) {
                 k.Emit(value);
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Loop<int>()
              .context(0)
              .body([](auto& count, auto& stream, auto&&) {
                if (++count == 2) {
                  stream.Done();
                } else {
                  stream.Next();
                }
              })
              .ended([](auto& count, auto& k) {
                k.Start(count);
              })
              .fail([&](auto&, auto&, auto&&) {
                fail.Call();
              })
              .stop([&](auto&, auto&) {
                stop.Call();
              });
  };

  EXPECT_EQ(2, *s());
}


TEST(StreamTest, Fail) {
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> stop, done, fail, ended;

  EXPECT_CALL(stop, Call())
      .Times(0);

  EXPECT_CALL(done, Call())
      .Times(0);

  EXPECT_CALL(fail, Call())
      .Times(0);

  EXPECT_CALL(ended, Call())
      .Times(0);

  auto s = [&]() {
    return Stream<int>()
               .context("error")
               .next([](auto& error, auto& k) {
                 k.Fail(error);
               })
               .done([&](auto&, auto&) {
                 done.Call();
               })
        | Loop<int>()
              .context(0)
              .body([](auto&, auto& stream, auto&&) {
                stream.Next();
              })
              .ended([&](auto&, auto&) {
                ended.Call();
              })
              .fail([&](auto&, auto& k, auto&& error) {
                k.Fail(std::forward<decltype(error)>(error));
              })
              .stop([&](auto&, auto&) {
                stop.Call();
              });
  };

  EXPECT_THROW(*s(), const char*);
}


TEST(StreamTest, InterruptStream) {
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> done, fail, ended;

  EXPECT_CALL(done, Call())
      .Times(0);

  EXPECT_CALL(fail, Call())
      .Times(0);

  EXPECT_CALL(ended, Call())
      .Times(0);

  std::atomic<bool> triggered = false;

  auto s = [&]() {
    return Stream<int>()
               .context(Context<std::atomic<bool>>(false))
               .next([](auto& interrupted, auto& k) {
                 if (!interrupted->load()) {
                   k.Emit(0);
                 } else {
                   k.Stop();
                 }
               })
               .done([&](auto&, auto&) {
                 done.Call();
               })
               .interrupt([](auto& interrupted, auto&) {
                 interrupted->store(true);
               })
        | Loop<int>()
              .body([&](auto& k, auto&&) {
                auto thread = std::thread(
                    [&]() mutable {
                      while (!triggered.load()) {
                        std::this_thread::yield();
                      }
                      k.Next();
                    });
                thread.detach();
              })
              .ended([&](auto&) {
                ended.Call();
              })
              .fail([&](auto&, auto&&) {
                fail.Call();
              })
              .stop([](auto& k) {
                k.Stop();
              });
  };

  auto [future, k] = Terminate(s());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  triggered.store(true);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(StreamTest, InterruptLoop) {
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> stop, fail, body;

  EXPECT_CALL(stop, Call())
      .Times(0);

  EXPECT_CALL(fail, Call())
      .Times(0);

  std::atomic<bool> triggered = false;

  auto s = [&]() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Emit(0);
               })
               .done([](auto& k) {
                 k.Ended();
               })
        | Loop<int>()
              .context(Context<std::atomic<bool>>(false))
              .body([&](auto&, auto& k, auto&&) {
                auto thread = std::thread(
                    [&]() mutable {
                      while (!triggered.load()) {
                        std::this_thread::yield();
                      }
                      k.Done();
                    });
                thread.detach();
              })
              .interrupt([](auto& interrupted, auto& k) {
                interrupted->store(true);
              })
              .ended([](auto& interrupted, auto& k) {
                if (interrupted->load()) {
                  k.Stop();
                } else {
                  k.Fail("error");
                }
              })
              .fail([&](auto&, auto&&) {
                fail.Call();
              })
              .stop([&](auto&) {
                stop.Call();
              });
  };

  auto [future, k] = Terminate(s());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  triggered.store(true);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(StreamTest, InfiniteLoop) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
        | Map(Then([](int i) { return i + 1; }))
        | Loop();
  };

  *s();
}


TEST(StreamTest, MapThenLoop) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
        | Map(Then([](int i) { return i + 1; }))
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(20, *s());
}


TEST(StreamTest, MapThenReduce) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Map(Then([](int i) {
             return i + 1;
           }))
        | Reduce(
               /* sum = */ 0,
               [](auto& sum) {
                 return Then([&](auto&& value) {
                   sum += value;
                   return true;
                 });
               });
  };

  EXPECT_EQ(20, *s());
}


TEST(StreamTest, MapEventualReduce) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | Map(Eventual<int>()
                  .start([](auto& k, auto&& i) {
                    k.Start(i + 1);
                  }))
        | Reduce(
               /* sum = */ 0,
               [](auto& sum) {
                 return Then([&](auto&& value) {
                   sum += value;
                   return true;
                 });
               });
  };

  EXPECT_EQ(20, *s());
}


TEST(StreamTest, Head) {
  auto s1 = []() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Emit(42);
               })
        | Head();
  };

  EXPECT_EQ(42, *s1());

  auto s2 = []() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Ended();
               })
        | Head();
  };

  EXPECT_THROW(*s2(), const char*);
}
