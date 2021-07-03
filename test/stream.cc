#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/context.h"
#include "stout/lambda.h"
#include "stout/reduce.h"
#include "stout/stream.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Context;
using stout::eventuals::done;
using stout::eventuals::ended;
using stout::eventuals::emit;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Lambda;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::next;
using stout::eventuals::Reduce;
using stout::eventuals::stop;
using stout::eventuals::Stream;
using stout::eventuals::succeed;
using stout::eventuals::Terminate;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(StreamTest, Succeed)
{
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
          emit(k, count--);
        } else {
          ended(k);
        }
      })
      .done([&](auto&, auto&) {
        done.Call();
      })
      | (Loop<int>()
         .context(0)
         .body([](auto& sum, auto& stream, auto&& value) {
           sum += value;
           next(stream);
         })
         .ended([](auto& sum, auto& k) {
           succeed(k, sum);
         })
         .fail([&](auto&, auto&, auto&&) {
           fail.Call();
         })
         .stop([&](auto&, auto&) {
           stop.Call();
         }));
  };

  EXPECT_EQ(15, *s());
}


TEST(StreamTest, Done)
{
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
        emit(k, value);
      })
      .done([](auto&, auto& k) {
        ended(k);
      })
      | (Loop<int>()
         .context(0)
         .body([](auto& count, auto& stream, auto&&) {
           if (++count == 2) {
             done(stream);
           } else {
             next(stream);
           }
         })
         .ended([](auto& count, auto& k) {
           succeed(k, count);
         })
         .fail([&](auto&, auto&, auto&&) {
           fail.Call();
         })
         .stop([&](auto&, auto&) {
           stop.Call();
         }));
  };

  EXPECT_EQ(2, *s());
}


TEST(StreamTest, Fail)
{
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
        eventuals::fail(k, error);
      })
      .done([&](auto&, auto&) {
        done.Call();
      })
      | (Loop<int>()
         .context(0)
         .body([](auto&, auto& stream, auto&&) {
           next(stream);
         })
         .ended([&](auto&, auto&) {
           ended.Call();
         })
         .fail([&](auto&, auto& k, auto&& error) {
           eventuals::fail(k, std::forward<decltype(error)>(error));
         })
         .stop([&](auto&, auto&) {
           stop.Call();
         }));
  };

  EXPECT_THROW(*s(), FailedException);
}


TEST(StreamTest, InterruptStream)
{
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
          emit(k, 0);
        } else {
          stop(k);
        }
      })
      .done([&](auto&, auto&) {
        done.Call();
      })
      .interrupt([](auto& interrupted, auto&) {
        interrupted->store(true);
      })
      | (Loop<int>()
         .body([&](auto& k, auto&&) {
           auto thread = std::thread(
               [&]() mutable {
                 while (!triggered.load()) {
                   std::this_thread::yield();
                 }
                 next(k);
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
           stop(k);
         }));
  };

  auto [future, t] = Terminate(s());

  Interrupt interrupt;

  t.Register(interrupt);

  t.Start();

  interrupt.Trigger();

  triggered.store(true);

  EXPECT_THROW(future.get(), StoppedException);
}


TEST(StreamTest, InterruptLoop)
{
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
        emit(k, 0);
      })
      .done([](auto& k) {
        ended(k);
      })
      | (Loop<int>()
         .context(Context<std::atomic<bool>>(false))
         .body([&](auto&, auto& k, auto&&) {
           auto thread = std::thread(
               [&]() mutable {
                 while (!triggered.load()) {
                   std::this_thread::yield();
                 }
                 done(k);
               });
           thread.detach();
         })
         .interrupt([](auto& interrupted, auto& k) {
           interrupted->store(true);
         })
         .ended([](auto& interrupted, auto& k) {
           if (interrupted->load()) {
             eventuals::stop(k);
           } else {
             eventuals::fail(k, "error");
           }
         })
         .fail([&](auto&, auto&&) {
           fail.Call();
         })
         .stop([&](auto&) {
           stop.Call();
         }));
  };

  auto [future, t] = Terminate(s());

  Interrupt interrupt;

  t.Register(interrupt);

  t.Start();

  interrupt.Trigger();

  triggered.store(true);

  EXPECT_THROW(future.get(), StoppedException);
}


TEST(StreamTest, InfiniteLoop)
{
  auto s = []() {
    return Stream<int>()
      .context(5)
      .next([](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      })
      | [](int i) { return i + 1; }
      | Loop();
  };

  *s();
}


TEST(StreamTest, Transform)
{
  auto s = []() {
    return Stream<int>()
      .context(5)
      .next([](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      })
      | [](int i) { return i + 1; }
      | (Loop<int>()
         .context(0)
         .body([](auto& sum, auto& stream, auto&& value) {
           sum += value;
           next(stream);
         })
         .ended([](auto& sum, auto& k) {
           succeed(k, sum);
         }));
  };

  EXPECT_EQ(20, *s());
}


TEST(StreamTest, MapReduceLambda)
{
  auto s = []() {
    return Stream<int>()
      .context(5)
      .next([](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      })
      .done([](auto&, auto& k) {
        ended(k);
      })
      | Map(Lambda([](int i) {
        return i + 1;
      }))
      | Reduce(
          /* sum = */ 0,
          [](auto& sum) {
            return [&](auto&& value) {
              sum += value;
              return true;
            };
          });
  };

  EXPECT_EQ(20, *s());
}


TEST(StreamTest, MapReduceEventual)
{
  auto s = [&]() {
    return Stream<int>()
      .context(5)
      .next([](auto& count, auto& k) {
        if (count > 0) {
          emit(k, count--);
        } else {
          ended(k);
        }
      })
      .done([](auto&, auto& k) {
        ended(k);
      })
      | Map(Eventual<int>()
            .start([](auto& k, auto&& i) {
              succeed(k, i + 1);
            }))
      | Reduce(
          /* sum = */ 0,
          [](auto& sum) {
            return [&](auto&& value) {
              sum += value;
              return true;
            };
          });
  };

  EXPECT_EQ(20, *s());
}
