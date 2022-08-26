#include "eventuals/do-all.hh"

#include "eventuals/eventual.hh"
#include "eventuals/task.hh"
#include "eventuals/then.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.hh"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(DoAllTest, Succeed) {
  auto e = []() {
    return DoAll(
        Eventual<int>([](auto& k) { k.Start(42); }),
        Eventual<std::string>([](auto& k) { k.Start(std::string("hello")); }),
        Eventual<void>([](auto& k) { k.Start(); }));
  };

  auto result = *e();

  using T = std::decay_t<decltype(result)>;

  EXPECT_EQ(std::tuple_size_v<T>, 3);

  using T0 = decltype(std::get<0>(result));
  using T1 = decltype(std::get<1>(result));
  using T2 = decltype(std::get<2>(result));

  static_assert(std::is_same_v<T0, int&>);
  static_assert(std::is_same_v<T1, std::string&>);
  static_assert(std::is_same_v<T2, std::monostate&>);

  EXPECT_EQ(std::make_tuple(42, "hello", std::monostate{}), result);
}


TEST(DoAllTest, SucceedTaskOf) {
  auto e = []() {
    return DoAll(
        Task::Of<int>([]() { return Just(42); }),
        Task::Of<std::string>([]() {
          return Eventual<std::string>(
              [](auto& k) { k.Start("hello"); });
        }),
        Task::Of<void>([]() { return Just(); }));
  };

  auto result = *e();

  using T = std::decay_t<decltype(result)>;

  EXPECT_EQ(std::tuple_size_v<T>, 3);

  using T0 = decltype(std::get<0>(result));
  using T1 = decltype(std::get<1>(result));
  using T2 = decltype(std::get<2>(result));

  static_assert(std::is_same_v<T0, int&>);
  static_assert(std::is_same_v<T1, std::string&>);
  static_assert(std::is_same_v<T2, std::monostate&>);

  EXPECT_EQ(std::make_tuple(42, "hello", std::monostate{}), result);
}


TEST(DoAllTest, Fail) {
  auto e = []() {
    return DoAll(
        Eventual<void>()
            .raises<std::runtime_error>()
            .start([](auto& k) { k.Fail(std::runtime_error("error")); }),
        Eventual<int>([](auto& k) { k.Start(42); }),
        Eventual<std::string>([](auto& k) { k.Start(std::string("hello")); }),
        Eventual<void>([](auto& k) { k.Start(); }));
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}


TEST(DoAllTest, Interrupt) {
  MockFunction<void()> start, fail;

  EXPECT_CALL(start, Call())
      .Times(1);

  EXPECT_CALL(fail, Call())
      .Times(0);

  auto e = [&start, &fail]() {
    return DoAll(Eventual<int>()
                     .interruptible()
                     .start([&start](auto& k, auto& handler) {
                       CHECK(handler)
                           << "Test expects interrupt to be registered";
                       start.Call();
                       k.Stop();
                     })
                     .fail([&fail](auto& k) {
                       fail.Call();
                     }));
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
