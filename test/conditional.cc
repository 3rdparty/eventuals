#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/conditional.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Conditional;
using stout::eventuals::Eventual;
using stout::eventuals::no;
using stout::eventuals::succeed;
using stout::eventuals::yes;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(ConditionalTest, Yes)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
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
      | (Conditional<std::string>(e())
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             yes(k);
           } else {
             no(k, "no");
           }
         }));
  };

  EXPECT_EQ("yes", eventuals::run(eventuals::task(c())));
}


TEST(ConditionalTest, No)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
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
      | (Conditional<std::string>(e())
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             yes(k);
           } else {
             no(k, "no");
           }
         }));
  };

  EXPECT_EQ("no", eventuals::run(eventuals::task(c())));
}


TEST(ConditionalTest, FailBeforeStart)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
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
      | (Conditional<std::string>(e())
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             yes(k);
           } else {
             no(k, "no");
           }
         }));
  };

  EXPECT_THROW(eventuals::run(eventuals::task(c())), FailedException);
}


TEST(ConditionalTest, FailAfterStart)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
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
      | (Conditional<std::string>(e())
         .start([](auto& k, auto&& i) {
           if (i > 1) {
             yes(k);
           } else {
             fail(k, "error");
           }
         }));
  };

  EXPECT_THROW(eventuals::run(eventuals::task(c())), FailedException);
}


TEST(ConditionalTest, Interrupt)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&]() {
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
      | (Conditional<std::string>(e())
         .start([](auto& k, auto&&) {
           yes(k);
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
