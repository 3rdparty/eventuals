#include "stout/closure.h"

#include <deque>
#include <string>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/just.h"
#include "stout/lambda.h"
#include "stout/map.h"
#include "stout/raise.h"
#include "stout/reduce.h"
#include "stout/repeat.h"
#include "stout/terminal.h"
#include "stout/until.h"

using std::deque;
using std::string;

namespace eventuals = stout::eventuals;

using stout::eventuals::Closure;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Just;
using stout::eventuals::Lambda;
using stout::eventuals::Map;
using stout::eventuals::Raise;
using stout::eventuals::Reduce;
using stout::eventuals::Repeat;
using stout::eventuals::Terminate;
using stout::eventuals::Until;

using testing::MockFunction;

TEST(ClosureTest, Lambda) {
  auto e = []() {
    return Just(1)
        | Closure([i = 41]() {
             return Lambda([&](auto&& value) { return i + value; });
           });
  };

  EXPECT_EQ(42, *e());
}


TEST(ClosureTest, Functor) {
  struct Functor {
    auto operator()() {
      return Lambda([this](auto&& value) { return i + value; });
    }

    int i;
  };

  auto e = []() {
    return Just(1)
        | Closure(Functor{41});
  };

  EXPECT_EQ(42, *e());
}


TEST(ClosureTest, OuterRepeat) {
  auto e = []() {
    return Repeat(Lambda([]() { return 1; }))
        | Closure([i = 41]() {
             return Reduce(
                 i,
                 [](auto& i) {
                   return Lambda([&](auto&& value) {
                     i += value;
                     return false;
                   });
                 });
           });
  };

  EXPECT_EQ(42, *e());
}


TEST(ClosureTest, InnerRepeat) {
  auto e = []() {
    return Closure([strings = deque<string>{"hello", "world"}]() mutable {
      return Repeat()
          | Until(Lambda([&]() {
               return strings.empty();
             }))
          | Map(Lambda([&]() mutable {
               auto s = std::move(strings.front());
               strings.pop_front();
               return s;
             }))
          | Reduce(
                 deque<string>(),
                 [](auto& results) {
                   return Lambda([&](auto&& result) mutable {
                     results.push_back(result);
                     return true;
                   });
                 });
    });
  };

  auto results = *e();

  ASSERT_EQ(2, results.size());

  EXPECT_EQ("hello", results[0]);
  EXPECT_EQ("world", results[1]);
}


TEST(ClosureTest, Fail) {
  auto e = []() {
    return Raise("error")
        | Closure([i = 41]() {
             return Lambda([&]() { return i + 1; });
           });
  };

  EXPECT_THROW(*e(), const char*);
}


TEST(ClosureTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&]() {
    return Just(1)
        | Closure([&]() {
             return Eventual<std::string>()
                 .start([&](auto&, auto&&) {
                   start.Call();
                 })
                 .interrupt([](auto& k) {
                   eventuals::stop(k);
                 });
           });
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  eventuals::start(k);

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
