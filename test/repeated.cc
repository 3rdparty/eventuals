#include <deque>
#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/repeated.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using std::deque;
using std::string;

using stout::eventuals::Eventual;
using stout::eventuals::Loop;
using stout::eventuals::repeat;
using stout::eventuals::Repeated;
using stout::eventuals::succeed;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(RepeatedTest, Succeed)
{
  auto e = [](auto s) {
    return Eventual<string>()
      .context(std::move(s))
      .start([](auto& s, auto& k) {
        succeed(k, std::move(s));
      });
  };

  auto r = [&]() {
    return Eventual<int>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, deque<string> { "hello", "world" });
            });
        thread.detach();
      })
      | (Repeated<string>([&](string s = "") { return e(s); })
         .context(deque<string>())
         .start([](auto& strings, auto& k, auto&& initializer) {
           strings = initializer;
           start(k);
         })
         .next([](auto& strings, auto& k) {
           if (!strings.empty()) {
             auto s = std::move(strings.front());
             strings.pop_front();
             repeat(k, std::move(s));
           } else {
             ended(k);
           }
         }))
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

  auto results = eventuals::run(eventuals::task(r()));

  ASSERT_EQ(2, results.size());

  EXPECT_EQ("hello", results[0]);
  EXPECT_EQ("world", results[1]);
}


TEST(RepeatedTest, Fail)
{
  auto e = [](auto) {
    return Eventual<string>()
      .start([](auto& k) {
        fail(k, "error");
      });
  };

  auto r = [&]() {
    return Eventual<int>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, deque<string> { "hello", "world" });
            });
        thread.detach();
      })
      | (Repeated<string>([&](string s = "") { return e(s); })
         .context(deque<string>())
         .start([](auto& strings, auto& k, auto&& initializer) {
           strings = initializer;
           start(k);
         })
         .next([](auto& strings, auto& k) {
           if (!strings.empty()) {
             auto s = std::move(strings.front());
             strings.pop_front();
             repeat(k, std::move(s));
           } else {
             ended(k);
           }
         }))
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

  EXPECT_THROW(eventuals::run(eventuals::task(r())), FailedException);
}


TEST(RepeatedTest, Interrupt)
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
    return Eventual<int>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, deque<string> { "hello", "world" });
            });
        thread.detach();
      })
      | (Repeated<string>([&](string s = "") { return e(s); })
         .context(deque<string>())
         .start([](auto& strings, auto& k, auto&& initializer) {
           strings = initializer;
           eventuals::start(k);
         })
         .next([](auto& strings, auto& k) {
           if (!strings.empty()) {
             auto s = std::move(strings.front());
             strings.pop_front();
             repeat(k, std::move(s));
           } else {
             ended(k);
           }
         }))
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

  auto t = eventuals::task(r());

  EXPECT_CALL(start, Call())
    .WillOnce([&]() {
      eventuals::interrupt(t);
    });

  eventuals::start(t);

  EXPECT_THROW(eventuals::wait(t), StoppedException);
}
