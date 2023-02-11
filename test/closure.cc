#include "eventuals/closure.h"

#include <deque>
#include <string>
#include <thread>

#include "eventuals/just.h"
#include "eventuals/map.h"
#include "eventuals/raise.h"
#include "eventuals/reduce.h"
#include "eventuals/repeat.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using std::deque;
using std::string;

using testing::ElementsAre;
using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(ClosureTest, Then) {
  auto e = []() {
    return Just(1)
        | Closure([i = 41]() {
             return Then([&](auto&& value) { return i + value; });
           });
  };

  EXPECT_EQ(42, *e());
}


TEST(ClosureTest, Functor) {
  struct Functor {
    auto operator()() {
      return Then([this](auto&& value) { return i + value; });
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
    return Repeat([]() { return 1; })
        | Closure([i = 41]() {
             return Reduce(
                 i,
                 [](auto& i) {
                   return Then([&](auto&& value) {
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
          | Until([&]() {
               return strings.empty();
             })
          | Map([&]() mutable {
               auto s = std::move(strings.front());
               strings.pop_front();
               return s;
             })
          | Reduce(
                 deque<string>(),
                 [](auto& results) {
                   return Then([&](auto&& result) mutable {
                     results.push_back(result);
                     return true;
                   });
                 });
    });
  };

  auto results = *e();

  EXPECT_THAT(results, ElementsAre("hello", "world"));
}


TEST(ClosureTest, Fail) {
  auto e = []() {
    return Raise("error")
        | Closure([i = 41]() {
             return Then([&]() { return i + 1; });
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}


TEST(ClosureTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&]() {
    return Just(1)
        | Closure([&]() {
             return Eventual<std::string>()
                 .interruptible()
                 .start([&](auto& k, auto& handler, auto&&) {
                   handler->Install([&k]() {
                     k.Stop();
                   });
                   start.Call();
                 });
           });
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
