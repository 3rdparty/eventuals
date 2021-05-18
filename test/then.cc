#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/task.h"
#include "stout/then.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Eventual;
using stout::eventuals::succeed;
using stout::eventuals::Then;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(ThenTest, Succeed)
{
  auto e = [](auto s) {
    return Eventual<std::string>()
      .context(std::move(s))
      .start([](auto& s, auto& k) {
        succeed(k, std::move(s));
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .context(1)
      .start([](auto& value, auto& k) {
        auto thread = std::thread(
            [&value, &k]() mutable {
              succeed(k, value);
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Then<std::string>([&](std::string s) { return e(std::move(s)); })
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             succeed(k, "then");
           } else {
             fail(k, "error");
           }
         }));
  };

  EXPECT_EQ("then", eventuals::run(eventuals::task(c())));
}


TEST(ThenTest, FailBeforeStart)
{
  auto e = [](auto s) {
    return Eventual<std::string>()
      .context(s)
      .start([](auto& s, auto& k) {
        succeed(k, std::move(s));
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
      | (Then<std::string>([&](std::string s) { return e(std::move(s)); })
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             succeed(k, "then");
           } else {
             fail(k, "error");
           }
         }));
  };

  EXPECT_THROW(eventuals::run(eventuals::task(c())), FailedException);
}


TEST(ThenTest, FailAfterStart)
{
  auto e = [](auto s) {
    return Eventual<std::string>()
      .context(s)
      .start([](auto& s, auto& k) {
        succeed(k, std::move(s));
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .context(0)
      .start([](auto& value, auto& k) {
        auto thread = std::thread(
            [&value, &k]() mutable {
              succeed(k, value);
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Then<std::string>([&](std::string s) { return e(std::move(s)); })
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             succeed(k, "then");
           } else {
             fail(k, "error");
           }
         }));
  };

  EXPECT_THROW(eventuals::run(eventuals::task(c())), FailedException);
}


TEST(ThenTest, Interrupt)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto) {
    return Eventual<std::string>()
      .start([&](auto&) {
        start.Call();
      })
      .interrupt([](auto& k) {
        stop(k);
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .start([](auto& k) {
        succeed(k, 0);
      })
      | [](int i) { return i + 1; }
      | (Then<std::string>([&](std::string s) { return e(std::move(s)); })
         .start([](auto& k, auto&&) {
           succeed(k, "then");
         }));
  };

  auto t = eventuals::task(c());

  EXPECT_CALL(start, Call())
    .WillOnce([&]() {
      eventuals::interrupt(t);
    });

  eventuals::start(t);

  EXPECT_THROW(eventuals::wait(t), StoppedException);
}
