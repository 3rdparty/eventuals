#include "eventuals/eventual.h"

#include <functional>
#include <memory>
#include <optional>
#include <thread>

#include "eventuals/catch.h"
#include "eventuals/expected.h"
#include "eventuals/just.h"
#include "eventuals/let.h"
#include "eventuals/raise.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

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
               .start([](int& context, auto& k) {
                 std::thread thread(
                     [&context, &k]() mutable {
                       k.Start(context);
                     });
                 thread.detach();
               })
        >> Then([](int i) { return i + 2; })
        >> Eventual<int>()
               .context(9)
               .start([](int& context, auto& k, int value) {
                 std::thread thread(
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
               .raises()
               .context("error")
               .start([](const char*& error, auto& k) {
                 std::thread thread(
                     [&error, &k]() mutable {
                       k.Fail(std::runtime_error(error));
                     });
                 thread.detach();
               })
        >> Then([](int i) { return i + 2; })
        >> Eventual<int>()
               .start([&](auto& k, int value) {
                 start.Call();
               })
               .stop([&](auto&) {
                 stop.Call();
               });
  };

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
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
               .interruptible()
               .start([&](
                          int&,
                          auto& k,
                          std::optional<Interrupt::Handler>& handler) {
                 CHECK(handler) << "Test expects interrupt to be registered";
                 EXPECT_TRUE(handler->Install([&k]() {
                   k.Stop();
                 }));
                 start.Call();
               })
        >> Then([](int i) { return i + 2; })
        >> Eventual<int>()
               .start([&](auto&, int) {
                 start.Call();
               })
               .fail([&](auto&, auto&&) {
                 fail.Call();
               })
               .stop([](auto& k) {
                 k.Stop();
               });
  };

  auto [future, k] = PromisifyForTest(e());

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
  auto operation = [](int i, std::promise<int>&& promise) {
    return (Eventual<int>()
                .context(i)
                .start([](int& context, auto& k) {
                  std::thread thread(
                      [&context, &k]() mutable {
                        k.Start(context);
                      });
                  thread.detach();
                }))
        >> Then([](int i) { return i + 2; })
        >> Eventual<int>()
               .context(9)
               .start([](int& context, auto& k, int value) {
                 std::thread thread(
                     [value, &context, &k]() mutable {
                       k.Start(context - value);
                     });
                 thread.detach();
               })
        >> Terminal()
               .context(std::move(promise))
               .start([](std::promise<int>& promise, int value) {
                 promise.set_value(std::forward<decltype(value)>(value));
               })
               .fail([](std::promise<int>& promise, auto&& error) {
                 promise.set_exception(
                     make_exception_ptr_or_forward(
                         std::forward<decltype(error)>(error)));
               })
               .stop([](std::promise<int>& promise) {
                 promise.set_exception(
                     std::make_exception_ptr(
                         eventuals::StoppedException()));
               });
  };

  using Operation = decltype(Build(operation(int(), std::promise<int>())));

  std::promise<int> promise1;

  auto future = promise1.get_future();

  auto o =
      std::make_unique<Operation>(Build(operation(5, std::move(promise1))));

  o->Start();

  EXPECT_EQ(2, future.get());

  std::promise<int> promise2;

  future = promise2.get_future();

  *o = Build(operation(4, std::move(promise2)));

  o->Start();

  EXPECT_EQ(3, future.get());
}


TEST(EventualTest, Raise) {
  auto e = []() {
    return Just(42)
        >> Raise("error")
        >> Raise("another error")
        >> Just(12);
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}


TEST(EventualTest, Catch) {
  auto e = []() {
    return Just(41)
        >> Raise("error")
        >> Catch([](std::exception_ptr&& error) {
             return 42;
           })
        >> Then([](int value) {
             return value;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(42, *e());
}


TEST(EventualTest, CatchVoid) {
  auto e = []() {
    return Just()
        >> Raise("error")
        >> Catch(Let([](auto& error) {
             return Then([&]() {
               EXPECT_EQ("error", What(error));
             });
           }))
        >> Then([]() {
             return 42;
           });
  };

  EXPECT_EQ(42, *e());
}


TEST(EventualTest, Then) {
  auto e = []() {
    return Just(20)
        >> Then([](int i) {
             return i + 1;
           })
        >> Then([](int j) {
             return j * 2;
           });
  };

  EXPECT_EQ(42, *e());
}


TEST(EventualTest, ConstRef) {
  int x = 10;

  auto e = [&]() {
    return Eventual<const int&>([&](auto& k) {
             k.Start(std::cref(x));
           })
        >> Then([](const int& x) {
             return std::cref(x);
           });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  x = 42;

  EXPECT_EQ(42, future.get());
}


TEST(EventualTest, Ref) {
  int x = 10;

  auto e = [&]() {
    return Eventual<int&>([&](auto& k) {
             k.Start(std::ref(x));
           })
        >> Then([](int& x) {
             x += 100;
           });
  };

  *e();

  EXPECT_EQ(110, x);
}

TEST(EventualTest, StaticHeapSize) {
  auto e = []() {
    return Eventual<int>([](auto& k) {
      k.Start(1);
    });
  };

  auto [_, k] = PromisifyForTest(e());

  EXPECT_EQ(0, k.StaticHeapSize().bytes());
}

} // namespace
} // namespace eventuals::test
