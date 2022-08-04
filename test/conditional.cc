#include "eventuals/conditional.h"

#include <thread>

#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using std::string;

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(ConditionalTest, Then) {
  auto then = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("then");
        });
  };

  auto els3 = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("else");
        });
  };

  auto c = [&]() {
    return Just(1)
        >> Then([](int i) { return i + 1; })
        >> Conditional(
               [](int&& i) { return i > 1; },
               [&](int&&) { return then(); },
               [&](int&&) { return els3(); });
  };

  EXPECT_EQ("then", *c());
}


TEST(ConditionalTest, Else) {
  auto then = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("then");
        });
  };

  auto els3 = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("else");
        });
  };

  auto c = [&]() {
    return Just(0)
        >> Then([](int i) { return i + 1; })
        >> Conditional(
               [](int&& i) { return i > 1; },
               [&](int&&) { return then(); },
               [&](int&&) { return els3(); });
  };

  EXPECT_EQ("else", *c());
}


TEST(ConditionalTest, Fail) {
  auto then = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("then");
        });
  };

  auto els3 = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("else");
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .raises<std::runtime_error>()
               .start([](auto& k) {
                 std::thread thread(
                     [&k]() mutable {
                       k.Fail(std::runtime_error("error"));
                     });
                 thread.detach();
               })
        >> Then([](int i) { return i + 1; })
        >> Conditional(
               [](int&& i) { return i > 1; },
               [&](int&&) { return then(); },
               [&](int&&) { return els3(); });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(c())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THAT(
      [&]() { *c(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}


TEST(ConditionalTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto then = [&]() {
    return Eventual<std::string>()
        .interruptible()
        .start([&](auto& k, auto& handler) {
          CHECK(handler) << "Test expects interrupt to be registered";
          handler->Install([&k]() {
            k.Stop();
          });
          start.Call();
        });
  };

  auto els3 = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("else");
        });
  };

  auto c = [&]() {
    return Just(1)
        >> Then([](int i) { return i + 1; })
        >> Conditional(
               [](int&& i) { return i > 1; },
               [&](int&&) { return then(); },
               [&](int&&) { return els3(); });
  };

  auto [future, k] = PromisifyForTest(c());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(ConditionalTest, Raise) {
  auto c = [&]() {
    return Just(1)
        >> Then([](int i) { return i + 1; })
        >> Conditional(
               [](int&& i) { return i > 1; },
               [](int&& i) { return Just(i); },
               [](int&& i) { return Raise("raise"); });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(c())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_EQ(2, *c());
}

} // namespace
} // namespace eventuals::test
