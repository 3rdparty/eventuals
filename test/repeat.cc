#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/lambda.h"
#include "stout/lock.h"
#include "stout/loop.h"
#include "stout/map.h"
#include "stout/reduce.h"
#include "stout/repeat.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "stout/until.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Acquire;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Lambda;
using stout::eventuals::Lock;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Reduce;
using stout::eventuals::Release;
using stout::eventuals::Repeat;
using stout::eventuals::Terminate;
using stout::eventuals::Then;
using stout::eventuals::Until;
using stout::eventuals::Wait;

using testing::MockFunction;

TEST(RepeatTest, Succeed)
{
  auto e = [](auto i) {
    return Eventual<int>()
      .context(i)
      .start([](auto& i, auto& k) {
        eventuals::succeed(k, std::move(i));
      });
  };

  auto r = [&]() {
    return Repeat(Lambda([i = 0]() mutable { return i++; }))
      | Until(Lambda([](auto& i) {
        return i == 5;
      }))
      | Map(Then([&](auto&& i) {
        return e(i);
      }))
      | Reduce(
          /* sum = */ 0,
          [](auto& sum) {
            return Lambda([&](auto&& i) {
              sum += i;
              return true;
            });
          });
  };

  EXPECT_EQ(10, *r());
}


TEST(RepeatTest, Fail)
{
  auto e = [](auto) {
    return Eventual<int>()
      .start([](auto& k) {
        eventuals::fail(k, "error");
      });
  };

  auto r = [&]() {
    return Repeat(Lambda([i = 0]() mutable { return i++; }))
      | Until(Lambda([](auto& i) {
        return i == 5;
      }))
      | Map(Then([&](auto&& i) {
        return e(i);
      }))
      | Reduce(
          /* sum = */ 0,
          [](auto& sum) {
            return Lambda([&](auto&& i) {
              sum += i;
              return true;
            });
          });
  };

  EXPECT_THROW(*r(), eventuals::FailedException);
}


TEST(RepeatTest, Interrupt)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto s) {
    return Eventual<int>()
      .start([&](auto& k) {
        start.Call();
      })
      .interrupt([](auto& k) {
        eventuals::stop(k);
      });
  };

  auto r = [&]() {
    return Repeat(Lambda([i = 0]() mutable { return i++; }))
      | Until(Lambda([](auto& i) {
        return i == 5;
      }))
      | Map(Then([&](auto&& i) {
        return e(i);
      }))
      | Reduce(
          /* sum = */ 0,
          [](auto& sum) {
            return Lambda([&](auto&& i) {
              sum += i;
              return true;
            });
          });
  };

  auto [future, k] = Terminate(r());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
    .WillOnce([&]() {
      interrupt.Trigger();
    });

  eventuals::start(k);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(RepeatTest, Eventual)
{
  auto r = []() {
    return Repeat(
        Eventual<int>()
        .start([](auto& k) {
          eventuals::succeed(k, 1);
        }))
      | (Loop<int>()
         .context(0)
         .body([](auto&& count, auto& repeated, auto&& value) {
           count += value;
           if (count >= 5) {
             eventuals::done(repeated);
           } else {
             eventuals::next(repeated);
           }
         })
         .ended([](auto& count, auto& k) {
           eventuals::succeed(k, std::move(count));
         }));
  };

  EXPECT_EQ(5, *r());
}


TEST(RepeatTest, Map)
{
  auto r = []() {
    return Repeat()
      | Map(Eventual<int>()
            .start([](auto& k) {
              eventuals::succeed(k, 1);
            }))
      | (Loop<int>()
         .context(0)
         .body([](auto&& count, auto& repeated, auto&& value) {
           count += value;
           if (count >= 5) {
             eventuals::done(repeated);
           } else {
             eventuals::next(repeated);
           }
         })
         .ended([](auto& count, auto& k) {
           eventuals::succeed(k, std::move(count));
         }));
  };

  EXPECT_EQ(5, *r());
}


TEST(RepeatTest, MapAcquire)
{
  Lock lock;

  auto r = [&]() {
    return Repeat(
        Eventual<int>()
        .start([](auto& k) {
          eventuals::succeed(k, 1);
        }))
      | Map(
          Acquire(&lock)
          | (Wait<int>(&lock)
             .condition([](auto& k, auto&& i) {
               eventuals::succeed(k, i);
             }))
          | Lambda([](auto&& i) {
            return i;
          })
          | Release(&lock))
      | (Loop<int>()
         .context(0)
         .body([](auto&& count, auto& repeated, auto&& value) {
           count += value;
           if (count >= 5) {
             eventuals::done(repeated);
           } else {
             eventuals::next(repeated);
           }
         })
         .ended([](auto& count, auto& k) {
           eventuals::succeed(k, std::move(count));
         }));
  };

  EXPECT_EQ(5, *r());
}
