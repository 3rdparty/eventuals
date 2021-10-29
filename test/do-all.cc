#include "eventuals/do-all.h"

#include "eventuals/eventual.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Build;
using eventuals::DoAll;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Terminal;
using eventuals::Terminate;
using eventuals::Then;

using testing::MockFunction;

TEST(DoAllTest, Succeed) {
  auto e = []() {
    return DoAll(
        Eventual<int>([](auto& k) { k.Start(42); }),
        Eventual<std::string>([](auto& k) { k.Start(std::string("hello")); }),
        Eventual<void>([](auto& k) { k.Start(); }));
  };

  auto result = *e();

  using T = std::decay_t<decltype(result)>;
  EXPECT_EQ(
      std::tuple_size_v<T>,
      3);
  EXPECT_EQ(
      std::get<0>(result).valueless_by_exception(),
      false);
  EXPECT_EQ(
      std::get<1>(result).valueless_by_exception(),
      false);
  EXPECT_EQ(
      std::get<2>(result).valueless_by_exception(),
      false);

  using T1 = decltype(std::get<0>(std::get<0>(result)));
  using T2 = decltype(std::get<0>(std::get<1>(result)));
  using T3 = decltype(std::get<0>(std::get<2>(result)));

  EXPECT_EQ((std::is_same_v<T1, int&>), true);
  EXPECT_EQ((std::is_same_v<T2, std::string&>), true);
  EXPECT_EQ((std::is_same_v<T3, std::monostate&>), true);
  EXPECT_EQ(
      (std::make_tuple(
          std::variant<int, std::exception_ptr>(42),
          std::variant<std::string, std::exception_ptr>("hello"),
          std::variant<std::monostate, std::exception_ptr>())),
      result);
}


TEST(DoAllTest, Fail) {
  auto e = []() {
    return DoAll(Eventual<void>([](auto& k) { k.Fail("error"); }));
  };

  auto result = *e();

  using T1 = decltype(result);
  using T2 = std::tuple<std::variant<
      std::monostate,
      std::exception_ptr>>;

  EXPECT_EQ(
      (std::tuple_size_v<std::decay_t<decltype(result)>>),
      1);
  EXPECT_EQ((std::is_same_v<T1, T2>), true);
  EXPECT_EQ(
      (std::holds_alternative<std::exception_ptr>(
          std::get<0>(result))),
      true);
}


TEST(DoAllTest, Interrupt) {
  MockFunction<void()> start, fail;

  EXPECT_CALL(start, Call())
      .Times(1);

  EXPECT_CALL(fail, Call())
      .Times(0);

  auto e = [&start, &fail]() {
    return DoAll(Eventual<void>()
                     .interruptible()
                     .start([&start](auto& k, Interrupt::Handler& handler) {
                       handler.Install([&k]() {
                         k.Stop();
                       });
                       start.Call();
                     })
                     .fail([&fail](auto& k) {
                       fail.Call();
                     }));
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  auto result = future.get();

  using T1 = decltype(result);
  using T2 = std::tuple<std::variant<
      std::monostate,
      std::exception_ptr>>;

  EXPECT_EQ(
      (std::tuple_size_v<std::decay_t<decltype(result)>>),
      1);
  EXPECT_EQ((std::is_same_v<T1, T2>), true);
  EXPECT_EQ(
      (std::holds_alternative<std::exception_ptr>(
          std::get<0>(result))),
      true);
}
