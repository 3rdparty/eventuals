#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/conditional.h"
#include "stout/return.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using std::string;

using stout::eventuals::Conditional;
using stout::eventuals::Eventual;
using stout::eventuals::Return;
using stout::eventuals::succeed;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;


TEST(ConditionalTest, Then)
{
  auto then = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "then");
      });
  };

  auto els3 = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "else");
      });
  };

  auto c = [&]() {
    return Return(1)
      | [](int i) { return i + 1; }
      | Conditional(
          [](auto&& i) { return i > 1; },
          [&](auto&&) { return then(); },
          [&](auto&&) { return els3(); });
  };

  EXPECT_EQ("then", *c());
}


TEST(ConditionalTest, Else)
{
  auto then = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "then");
      });
  };

  auto els3 = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "else");
      });
  };

  auto c = [&]() {
    return Return(0)
      | [](int i) { return i + 1; }
      | Conditional(
          [](auto&& i) { return i > 1; },
          [&](auto&&) { return then(); },
          [&](auto&&) { return els3(); });
  };

  EXPECT_EQ("else", *c());
}


TEST(ConditionalTest, Fail)
{
  auto then = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "then");
      });
  };

  auto els3 = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "else");
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              fail(k, "error");
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | Conditional(
          [](auto&& i) { return i > 1; },
          [&](auto&&) { return then(); },
          [&](auto&&) { return els3(); });
  };

  EXPECT_THROW(*c(), FailedException);
}


TEST(ConditionalTest, Interrupt)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto then = [&]() {
    return Eventual<std::string>()
      .start([&](auto&) {
        start.Call();
      })
      .interrupt([](auto& k) {
        stop(k);
      });
  };

  auto els3 = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "else");
      });
  };

  auto c = [&]() {
    return Return(1)
      | [](int i) { return i + 1; }
      | Conditional(
          [](auto&& i) { return i > 1; },
          [&](auto&&) { return then(); },
          [&](auto&&) { return els3(); });
  };

  auto t = eventuals::TaskFrom(c());

  EXPECT_CALL(start, Call())
    .WillOnce([&]() {
      t.Interrupt();
    });

  t.Start();

  EXPECT_THROW(t.Wait(), StoppedException);
}
