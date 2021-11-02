#include "eventuals/finally.h"

#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/terminal.h"
#include "gtest/gtest.h"

using eventuals::Eventual;
using eventuals::Finally;
using eventuals::Just;
using eventuals::Raise;

TEST(Finally, Succeed) {
  auto e = []() {
    return Just(42)
        | Finally([](auto&& value) {
             return std::move(value);
           });
  };

  auto result = *e();

  using T0 = decltype(std::get<0>(result));
  using T1 = decltype(std::get<1>(result));

  static_assert(std::is_same_v<T0, int&>);
  static_assert(std::is_same_v<T1, std::exception_ptr&>);

  EXPECT_EQ(
      (std::variant<int, std::exception_ptr>(42)),
      result);
}

TEST(Finally, Fail) {
  auto e = []() {
    return Just(42)
        | Raise("error")
        | Finally([](auto&& value) {
             return std::move(value);
           });
  };

  auto result = *e();

  using T0 = decltype(std::get<0>(result));
  using T1 = decltype(std::get<1>(result));

  static_assert(std::is_same_v<T0, int&>);
  static_assert(std::is_same_v<T1, std::exception_ptr&>);

  ASSERT_EQ(1, result.index());

  EXPECT_THROW(
      std::rethrow_exception(std::get<1>(result)),
      const char*);
}

TEST(Finally, Stop) {
  auto e = []() {
    return Eventual<std::string>([](auto& k) {
             k.Stop();
           })
        | Finally([](auto&& value) {
             return std::move(value);
           });
  };

  auto result = *e();

  using T0 = decltype(std::get<0>(result));
  using T1 = decltype(std::get<1>(result));

  static_assert(std::is_same_v<T0, std::string&>);
  static_assert(std::is_same_v<T1, std::exception_ptr&>);

  ASSERT_EQ(1, result.index());

  EXPECT_THROW(
      std::rethrow_exception(std::get<1>(result)),
      eventuals::StoppedException);
}

TEST(Finally, VoidSucceed) {
  auto e = []() {
    return Just()
        | Finally([](auto&& value) {
             return std::move(value);
           });
  };

  auto result = *e();

  using T = decltype(result);

  static_assert(std::is_same_v<T, std::optional<std::exception_ptr>>);

  EXPECT_FALSE(result.has_value());
}

TEST(Finally, VoidFail) {
  auto e = []() {
    return Just()
        | Raise("error")
        | Finally([](auto&& value) {
             return std::move(value);
           });
  };

  auto result = *e();

  using T = decltype(result);

  static_assert(std::is_same_v<T, std::optional<std::exception_ptr>>);

  ASSERT_TRUE(result.has_value());

  EXPECT_THROW(
      std::rethrow_exception(result.value()),
      const char*);
}

TEST(Finally, VoidStop) {
  auto e = []() {
    return Eventual<void>([](auto& k) {
             k.Stop();
           })
        | Finally([](auto&& value) {
             return std::move(value);
           });
  };

  auto result = *e();

  using T = decltype(result);

  static_assert(std::is_same_v<T, std::optional<std::exception_ptr>>);

  ASSERT_TRUE(result.has_value());

  EXPECT_THROW(
      std::rethrow_exception(result.value()),
      eventuals::StoppedException);
}
