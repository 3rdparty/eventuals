#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/eventual.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Eventual;
using stout::eventuals::succeed;
using stout::eventuals::Terminal;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(EventualTest, Succeed)
{
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto e = Eventual<int>()
    .context(5)
    .start([](auto& context, auto& k) {
      auto thread = std::thread(
          [&context, &k]() mutable {
            succeed(k, context);
          });
      thread.detach();
    })
    .stop([&](auto&, auto&) {
      stop.Call();
    })
    | [](int i) { return i + 2; }
    | (Eventual<int>()
       .context(9)
       .start([](auto& context, auto& k, auto&& value) {
         auto thread = std::thread(
             [value, &context, &k]() mutable {
               succeed(k, context - value);
             });
         thread.detach();
       })
       .fail([&](auto&, auto&, auto&&) {
         fail.Call();
       })
       .stop([&](auto&, auto&) {
         stop.Call();
       }));

  EXPECT_EQ(2, eventuals::run(eventuals::task(e)));
}


TEST(EventualTest, Fail)
{
  // Using mocks to ensure start and stop callback don't get invoked.
  MockFunction<void()> start, stop;

  EXPECT_CALL(start, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto e = Eventual<int>()
    .context("error")
    .start([](auto& error, auto& k) {
      auto thread = std::thread(
          [&error, &k]() mutable {
            fail(k, error);
          });
      thread.detach();
    })
    .stop([&](auto&, auto&) {
      stop.Call();
    })
    | [](int i) { return i + 2; }
    | (Eventual<int>()
       .start([&](auto& k, auto&& value) {
         start.Call();
       })
       .stop([&](auto&) {
         stop.Call();
       }));

  EXPECT_THROW(eventuals::run(eventuals::task(e)), FailedException);
}


TEST(EventualTest, Stopped)
{
  // Using mocks to ensure start is only called once and fail
  // callbacks don't get invoked.
  MockFunction<void()> start, fail;

  EXPECT_CALL(start, Call())
    .Times(1);

  EXPECT_CALL(fail, Call())
    .Times(0);

  auto e = Eventual<int>()
    .context(5)
    .start([&](auto&, auto& k) {
      start.Call();
    })
    .stop([](auto&, auto& k) {
      stop(k);
    })
    | [](int i) { return i + 2; }
    | (Eventual<int>()
       .start([&](auto&, auto&&) {
         start.Call();
       })
       .fail([&](auto&, auto&&) {
         fail.Call();
       })
       .stop([](auto& k) {
         stop(k);
       }));

  auto t = eventuals::task(e);

  eventuals::start(t);

  eventuals::stop(t);

  EXPECT_THROW(eventuals::wait(t), StoppedException);
}


TEST(EventualTest, Reuse)
{
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto operation = [&](int i, auto&& promise) {
    return (Eventual<int>()
            .context(i)
            .start([](auto& context, auto& k) {
              auto thread = std::thread(
                  [&context, &k]() mutable {
                    succeed(k, context);
                  });
              thread.detach();
            })
            .stop([&](auto&, auto&) {
              stop.Call();
            }))
      | [](int i) { return i + 2; }
      | (Eventual<int>()
         .context(9)
         .start([](auto& context, auto& k, auto&& value) {
           auto thread = std::thread(
               [value, &context, &k]() mutable {
                 succeed(k, context - value);
               });
           thread.detach();
         })
         .fail([&](auto&, auto&, auto&&) {
           fail.Call();
         })
         .stop([&](auto&, auto&) {
           stop.Call();
         }))
      | (Terminal()
         .context(std::move(promise))
         .start([](auto& promise, auto&& value) {
           promise.set_value(std::forward<decltype(value)>(value));
         })
         .fail([](auto& promise, auto&& error) {
           promise.set_exception(std::make_exception_ptr(FailedException()));
         })
         .stop([](auto& promise) {
           promise.set_exception(std::make_exception_ptr(StoppedException()));
         }));
  };

  using Operation = decltype(operation(int(), std::promise<int>()));

  std::promise<int> promise1;

  auto future = promise1.get_future();

  auto* o = new Operation(operation(5, std::move(promise1)));

  eventuals::start(*o);

  EXPECT_EQ(2, future.get());

  std::promise<int> promise2;

  future = promise2.get_future();

  *o = operation(4, std::move(promise2));

  eventuals::start(*o);

  EXPECT_EQ(3, future.get());

  delete o;
}
