#include "eventuals/if.h"

#include <thread>

#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(IfTest, Yes) {
  auto e = []() {
    return Just(1)
        >> Then([](int i) {
             return If(i == 1)
                 .yes([]() { return Just("yes"); })
                 .no([]() { return Just("no"); });
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ("yes", *e());
}


TEST(IfTest, No) {
  auto e = []() {
    return Just(0)
        >> Then([](int i) {
             return If(i == 1)
                 .yes([]() { return Just("yes"); })
                 .no([]() { return Just("no"); });
           });
  };

  EXPECT_EQ("no", *e());
}


TEST(IfTest, Fail) {
  auto e = []() {
    return Just(0)
        >> Raise("error")
        >> Then([](int i) {
             return If(i == 1)
                 .yes([]() { return Just("yes"); })
                 .no([]() { return Just("no"); });
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


TEST(IfTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&]() {
    return Just(1)
        >> Then([&](int i) {
             return If(i == 1)
                 .yes([&]() {
                   return Eventual<const char*>()
                       .interruptible()
                       .start([&](auto& k, auto& handler) {
                         CHECK(handler)
                             << "Test expects interrupt to be registered";
                         handler->Install([&k]() {
                           k.Stop();
                         });
                         start.Call();
                       });
                 })
                 .no([]() { return Just("no"); });
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


TEST(IfTest, Raise) {
  auto e = []() {
    return Just(1)
        >> Then([](int i) {
             return If(i == 1)
                 .yes([]() { return 42; })
                 .no([]() { return Raise("raise"); });
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_EQ(42, *e());
}


TEST(IfTest, StaticHeapSize) {
  auto e = []() {
    return Just(1)
        >> Then([](int i) {
             return If(i == 1)
                 .yes([]() { return Just("yes"); })
                 .no([]() { return Just("no"); });
           });
  };

  auto [_, k] = PromisifyForTest(e());

  EXPECT_EQ(0, k.StaticHeapSize().bytes());
}

} // namespace
} // namespace eventuals::test
