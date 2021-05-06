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
  auto value = 1;

  auto e = Eventual<std::string>()
    .start([](auto& k) {
      succeed(k, "yes");
    });

  auto c = Eventual<int>()
    .context(value)
    .start([](auto& value, auto& k) {
      auto thread = std::thread(
          [&value, &k]() mutable {
            succeed(k, value);
          });
      thread.detach();
    })
    | [](int i) { return i + 1; }
    | (Conditional<std::string>(std::move(e))
       .start([](auto& k, auto&& i) {
         if (i > 1) {
           yes(k);
         } else {
           no(k, "no");
         }
       }));

  EXPECT_EQ("yes", eventuals::run(eventuals::task(c)));
}


TEST(ConditionalTest, No)
{
  auto value = 0;

  auto e = Eventual<std::string>()
    .start([](auto& k) {
      succeed(k, "yes");
    });

  auto c = Eventual<int>()
    .context(value)
    .start([](auto& value, auto& k) {
      auto thread = std::thread(
          [&value, &k]() mutable {
            succeed(k, value);
          });
      thread.detach();
    })
    | [](int i) { return i + 1; }
    | (Conditional<std::string>(std::move(e))
       .start([](auto& k, auto&& i) {
         if (i > 1) {
           yes(k);
         } else {
           no(k, "no");
         }
       }));

  EXPECT_EQ("no", eventuals::run(eventuals::task(c)));
}


TEST(ConditionalTest, Fail)
{
  auto value = 0;

  auto e = Eventual<std::string>()
    .start([](auto& k) {
      succeed(k, "yes");
    });

  auto c = Eventual<int>()
    .context(value)
    .start([](auto& value, auto& k) {
      auto thread = std::thread(
          [&k]() mutable {
            fail(k, "error");
          });
      thread.detach();
    })
    | [](int i) { return i + 1; }
    | (Conditional<std::string>(std::move(e))
       .start([](auto& k, auto&& i) {
         if (i > 1) {
           yes(k);
         } else {
           no(k, "no");
         }
       }));

  EXPECT_THROW(eventuals::run(eventuals::task(c)), FailedException);
}


TEST(ConditionalTest, Stop)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  EXPECT_CALL(start, Call())
    .Times(1);

  auto value = 0;

  auto e = Eventual<std::string>()
    .start([](auto& k) {
      succeed(k, "yes");
    });

  auto c = Eventual<int>()
    .context(value)
    .start([&](auto& value, auto& k) {
      start.Call();
    })
    .stop([](auto&, auto& k) {
      stop(k);
    })
    | [](int i) { return i + 1; }
    | (Conditional<std::string>(std::move(e))
       .start([](auto& k, auto&& i) {
         if (i > 1) {
           yes(k);
         } else {
           no(k, "no");
         }
       }));

  auto t = eventuals::task(std::move(c));

  eventuals::start(t);

  eventuals::stop(t);

  EXPECT_THROW(eventuals::wait(t), StoppedException);
}
