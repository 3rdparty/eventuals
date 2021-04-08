#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/eventual.h"
#include "stout/task.h"

using namespace stout;

using stout::eventuals::continuation;
using stout::eventuals::eventual;
using stout::eventuals::terminal;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;


TEST(EventualTest, Succeeded)
{
  // Using mocks to ensure fail and stop callbacks don't get invoked.
  MockFunction<void()> fail, stop;

  EXPECT_CALL(fail, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto i = eventual<int>(
      5,
      [](auto& context, auto& k) {
        auto thread = std::thread(
            [&context, &k]() mutable {
              succeeded(k, context);
            });
        thread.detach();
      },
      [&](auto&, auto&) {
        stop.Call();
      });

  auto j = i
    | [](int i) { return i + 2; }
    | continuation<int>(
        9,
        [](auto& context, auto& k, auto&& value) {
          auto thread = std::thread(
              [value, &context, &k]() mutable {
                succeeded(k, context - value);
              });
          thread.detach();
        },
        [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_EQ(2, eventuals::run(eventuals::task(j)));
}


TEST(EventualTest, Failed)
{
  // Using mocks to ensure start and stop callback don't get invoked.
  MockFunction<void()> start, stop;

  EXPECT_CALL(start, Call())
    .Times(0);

  EXPECT_CALL(stop, Call())
    .Times(0);

  auto i = eventual<int>(
      5,
      [](auto& context, auto& k) {
        auto thread = std::thread(
            [&context, &k]() mutable {
              failed(k, context);
            });
        thread.detach();
      },
      [&](auto&, auto&) {
        stop.Call();
      });

  auto j = i
    | [](int i) { return i + 2; }
    | continuation<int>(
        [&](auto&, auto&, auto&&) {
          start.Call();
        },
        [](auto&, auto& k, auto&& error) {
          failed(k, std::forward<decltype(error)>(error));
        },
        [&](auto&, auto&) {
          stop.Call();
        });

  EXPECT_THROW(eventuals::run(eventuals::task(j)), FailedException);
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

  auto i = eventual<int>(
      5,
      [&](auto&, auto& k) {
        start.Call();
      },
      [](auto&, auto& k) {
        stopped(k);
      });

  auto j = i
    | [](int i) { return i + 2; }
    | continuation<int>(
        [&](auto&, auto&, auto&&) {
          start.Call();
        },
        [&](auto&, auto&, auto&&) {
          fail.Call();
        },
        [](auto&, auto& k) {
          stopped(k);
        });

  auto t = eventuals::task(j);

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
    return eventual<int>(
        i,
        [](auto& context, auto& k) {
          auto thread = std::thread(
              [&context, &k]() mutable {
                succeeded(k, context);
              });
          thread.detach();
        },
        [&](auto&, auto&) {
          stop.Call();
        })
      | [](int i) { return i + 2; }
      | continuation<int>(
          9,
          [](auto& context, auto& k, auto&& value) {
            auto thread = std::thread(
                [value, &context, &k]() mutable {
                  succeeded(k, context - value);
                });
            thread.detach();
          },
          [&](auto&, auto&, auto&&) {
            fail.Call();
          },
          [&](auto&, auto&) {
            stop.Call();
          })
      | terminal(
          std::move(promise),
          [](auto& promise, auto&& value) {
            promise.set_value(std::forward<decltype(value)>(value));
          },
          [](auto& promise, auto&& error) {
            promise.set_exception(std::make_exception_ptr(FailedException()));
          },
          [](auto& promise) {
            promise.set_exception(std::make_exception_ptr(StoppedException()));
          });
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
