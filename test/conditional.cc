#include "eventuals/conditional.h"

#include <thread>

#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using std::string;

using eventuals::Conditional;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Just;
using eventuals::Raise;
using eventuals::Terminate;
using eventuals::Then;

using testing::MockFunction;

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
        | Then([](int i) { return i + 1; })
        | Conditional(
               [](auto&& i) { return i > 1; },
               [&](auto&&) { return then(); },
               [&](auto&&) { return els3(); });
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
        | Then([](int i) { return i + 1; })
        | Conditional(
               [](auto&& i) { return i > 1; },
               [&](auto&&) { return then(); },
               [&](auto&&) { return els3(); });
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
               .start([](auto& k) {
                 auto thread = std::thread(
                     [&k]() mutable {
                       k.Fail("error");
                     });
                 thread.detach();
               })
        | Then([](int i) { return i + 1; })
        | Conditional(
               [](auto&& i) { return i > 1; },
               [&](auto&&) { return then(); },
               [&](auto&&) { return els3(); });
  };

  EXPECT_THROW(*c(), const char*);
}


TEST(ConditionalTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto then = [&]() {
    return Eventual<std::string>()
        .start([&](auto&) {
          start.Call();
        })
        .interrupt([](auto& k) {
          k.Stop();
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
        | Then([](int i) { return i + 1; })
        | Conditional(
               [](auto&& i) { return i > 1; },
               [&](auto&&) { return then(); },
               [&](auto&&) { return els3(); });
  };

  auto [future, k] = Terminate(c());

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
        | Then([](int i) { return i + 1; })
        | Conditional(
               [](auto&& i) { return i > 1; },
               [](auto&& i) { return Just(i); },
               [](auto&& i) { return Raise("raise"); });
  };

  EXPECT_EQ(2, *c());
}
