#include <deque>
#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/lambda.h"
#include "stout/lock.h"
#include "stout/map.h"
#include "stout/repeat.h"
#include "stout/task.h"
#include "stout/transform.h"
#include "stout/until.h"

namespace eventuals = stout::eventuals;

using std::deque;
using std::string;

using stout::eventuals::Acquire;
using stout::eventuals::done;
using stout::eventuals::Eventual;
using stout::eventuals::fail;
using stout::eventuals::Interrupt;
using stout::eventuals::Lambda;
using stout::eventuals::Lock;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::next;
using stout::eventuals::Release;
using stout::eventuals::Repeat;
using stout::eventuals::stop;
using stout::eventuals::succeed;
using stout::eventuals::Terminate;
using stout::eventuals::Until;
using stout::eventuals::Wait;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(RepeatTest, Succeed)
{
  auto e = [](auto s) {
    return Eventual<string>()
      .context(std::move(s))
      .start([](auto& s, auto& k) {
        succeed(k, std::move(s));
      });
  };

  auto r = [&]() {
    return Eventual<deque<string>>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, deque<string> { "hello", "world" });
            });
        thread.detach();
      })
      | Repeat()
      | Until([](auto& strings) {
        return strings.empty();
      })
      | Map([&](auto&& strings) {
        auto s = std::move(strings.front());
        strings.pop_front();
        return e(std::move(s));
      })
      | (Loop<deque<string>>()
         .context(deque<string>())
         .body([](auto& results, auto& repeated, auto&& result) {
           results.push_back(result);
           next(repeated);
         })
         .ended([](auto& results, auto& k) {
           succeed(k, std::move(results));
         }));
  };

  auto results = *r();

  ASSERT_EQ(2, results.size());

  EXPECT_EQ("hello", results[0]);
  EXPECT_EQ("world", results[1]);
}


TEST(RepeatTest, Fail)
{
  auto e = [](auto) {
    return Eventual<string>()
      .start([](auto& k) {
        fail(k, "error");
      });
  };

  auto r = [&]() {
    return Eventual<deque<string>>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, deque<string> { "hello", "world" });
            });
        thread.detach();
      })
      | Repeat()
      | Until([](auto& strings) {
        return strings.empty();
      })
      | Map([&](auto&& strings) {
        auto s = std::move(strings.front());
        strings.pop_front();
        return e(std::move(s));
      })
      | (Loop<deque<string>>()
         .context(deque<string>())
         .body([](auto& results, auto& repeated, auto&& result) {
           results.push_back(result);
           next(repeated);
         })
         .ended([](auto& results, auto& k) {
           succeed(k, std::move(results));
         }));
  };

  EXPECT_THROW(*r(), FailedException);
}


TEST(RepeatTest, Interrupt)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto s) {
    return Eventual<string>()
      .start([&](auto& k) {
        start.Call();
      })
      .interrupt([](auto& k) {
        stop(k);
      });
  };

  auto r = [&]() {
    return Eventual<deque<string>>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, deque<string> { "hello", "world" });
            });
        thread.detach();
      })
      | Repeat()
      | Until([](auto& strings) {
        return strings.empty();
      })
      | Map([&](auto&& strings) {
        auto s = std::move(strings.front());
        strings.pop_front();
        return e(std::move(s));
      })
      | (Loop<deque<string>>()
         .context(deque<string>())
         .body([](auto& results, auto& repeated, auto&& result) {
           results.push_back(result);
           next(repeated);
         })
         .ended([](auto& results, auto& k) {
           succeed(k, std::move(results));
         }));
  };

  auto [future, t] = Terminate(r());

  Interrupt interrupt;

  t.Register(interrupt);

  EXPECT_CALL(start, Call())
    .WillOnce([&]() {
      interrupt.Trigger();
    });

  t.Start();

  EXPECT_THROW(future.get(), StoppedException);
}


TEST(RepeatTest, Eventual)
{
  auto r = []() {
    return Repeat(
        Eventual<int>()
        .start([](auto& k) {
          succeed(k, 1);
        }))
      | (Loop<int>()
         .context(0)
         .body([](auto&& count, auto& repeated, auto&& value) {
           count += value;
           if (count >= 5) {
             done(repeated);
           } else {
             next(repeated);
           }
         })
         .ended([](auto& count, auto& k) {
           succeed(k, std::move(count));
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
          succeed(k, 1);
        }))
      | (Loop<int>()
         .context(0)
         .body([](auto&& count, auto& repeated, auto&& value) {
           count += value;
           if (count >= 5) {
             done(repeated);
           } else {
             next(repeated);
           }
         })
         .ended([](auto& count, auto& k) {
           succeed(k, std::move(count));
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
          succeed(k, 1);
        }))
      | Map(
            Acquire(&lock)
            | (Wait<int>(&lock)
               .condition([](auto& k, auto&& i) {
                 succeed(k, i);
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
             done(repeated);
           } else {
             next(repeated);
           }
         })
         .ended([](auto& count, auto& k) {
           succeed(k, std::move(count));
         }));
  };

  EXPECT_EQ(5, *r());
}
