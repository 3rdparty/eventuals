#include "stout/eventual.h"

#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/catch.h"
#include "stout/just.h"
#include "stout/lambda.h"
#include "stout/raise.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Build;
using stout::eventuals::Catch;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Just;
using stout::eventuals::Lambda;
using stout::eventuals::Raise;
using stout::eventuals::Terminal;
using stout::eventuals::Terminate;

using testing::MockFunction;

TEST(EventualTest, Succeed) {
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
      .Times(0);

  EXPECT_CALL(stop, Call())
      .Times(0);

  auto e = [&]() {
    return Eventual<int>()
               .context(5)
               .start([](auto& context, auto& k) {
                 auto thread = std::thread(
                     [&context, &k]() mutable {
                       k.Start(context);
                     });
                 thread.detach();
               })
        | Lambda([](int i) { return i + 2; })
        | Eventual<int>()
              .context(9)
              .start([](auto& context, auto& k, auto&& value) {
                auto thread = std::thread(
                    [value, &context, &k]() mutable {
                      k.Start(context - value);
                    });
                thread.detach();
              })
              .fail([&](auto&, auto&, auto&&) {
                fail.Call();
              })
              .stop([&](auto&, auto&) {
                stop.Call();
              });
  };

  EXPECT_EQ(2, *e());
}


TEST(EventualTest, Fail) {
  // Using mocks to ensure start and stop callback don't get invoked.
  MockFunction<void()> start, stop;

  EXPECT_CALL(start, Call())
      .Times(0);

  EXPECT_CALL(stop, Call())
      .Times(0);

  auto e = [&]() {
    return Eventual<int>()
               .context("error")
               .start([](auto& error, auto& k) {
                 auto thread = std::thread(
                     [&error, &k]() mutable {
                       k.Fail(error);
                     });
                 thread.detach();
               })
        | Lambda([](int i) { return i + 2; })
        | Eventual<int>()
              .start([&](auto& k, auto&& value) {
                start.Call();
              })
              .stop([&](auto&) {
                stop.Call();
              });
  };

  EXPECT_THROW(*e(), const char*);
}


TEST(EventualTest, Interrupt) {
  // Using mocks to ensure start is only called once and fail
  // callbacks don't get invoked.
  MockFunction<void()> start, fail;

  EXPECT_CALL(fail, Call())
      .Times(0);

  auto e = [&]() {
    return Eventual<int>()
               .context(5)
               .start([&](auto&, auto& k) {
                 start.Call();
               })
               .interrupt([](auto&, auto& k) {
                 k.Stop();
               })
        | Lambda([](int i) { return i + 2; })
        | Eventual<int>()
              .start([&](auto&, auto&&) {
                start.Call();
              })
              .fail([&](auto&, auto&&) {
                fail.Call();
              })
              .stop([](auto& k) {
                k.Stop();
              });
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(EventualTest, Reuse) {
  auto operation = [](int i, auto&& promise) {
    return (Eventual<int>()
                .context(i)
                .start([](auto& context, auto& k) {
                  auto thread = std::thread(
                      [&context, &k]() mutable {
                        k.Start(context);
                      });
                  thread.detach();
                }))
        | Lambda([](int i) { return i + 2; })
        | Eventual<int>()
              .context(9)
              .start([](auto& context, auto& k, auto&& value) {
                auto thread = std::thread(
                    [value, &context, &k]() mutable {
                      k.Start(context - value);
                    });
                thread.detach();
              })
        | Terminal()
              .context(std::move(promise))
              .start([](auto& promise, auto&& value) {
                promise.set_value(std::forward<decltype(value)>(value));
              })
              .fail([](auto& promise, auto&& error) {
                promise.set_exception(
                    std::make_exception_ptr(
                        std::forward<decltype(error)>(error)));
              })
              .stop([](auto& promise) {
                promise.set_exception(
                    std::make_exception_ptr(
                        eventuals::StoppedException()));
              });
  };

  using Operation = decltype(Build(operation(int(), std::promise<int>())));

  std::promise<int> promise1;

  auto future = promise1.get_future();

  auto* o = new Operation(Build(operation(5, std::move(promise1))));

  o->Start();

  EXPECT_EQ(2, future.get());

  std::promise<int> promise2;

  future = promise2.get_future();

  *o = Build(operation(4, std::move(promise2)));

  o->Start();

  EXPECT_EQ(3, future.get());

  delete o;
}


TEST(EventualTest, Just) {
  auto e = []() {
    return Just(42);
  };

  EXPECT_EQ(42, *e());
}


TEST(EventualTest, Raise) {
  auto e = []() {
    return Just(42)
        | Raise("error");
  };

  EXPECT_THROW(*e(), const char*);
}


TEST(EventualTest, Catch) {
  auto e = []() {
    return Just(41)
        | Raise("error")
        | Catch([](auto&& error) {
             return Just(42);
           });
  };

  EXPECT_EQ(42, *e());
}


TEST(EventualTest, Lambda) {
  auto e = []() {
    return Just(42)
        | Lambda([](auto i) {
             return i + 1;
           })
        | Lambda([](auto j) {
             return j - 1;
           });
  };

  EXPECT_EQ(42, *e());
}
