#include "eventuals/expected.h"

#include <string>

#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

TEST(Expected, Construct) {
  Expected::Of<std::string> s = "hello world";

  ASSERT_TRUE(s.has_value());
  EXPECT_EQ("hello world", *s);

  s = Expected::Of<std::string>(s);

  ASSERT_TRUE(s.has_value());
  EXPECT_EQ("hello world", *s);

  s = Expected::Of<std::string>(std::move(s));

  ASSERT_TRUE(s.has_value());
  EXPECT_EQ("hello world", *s);
}

TEST(Expected, Match) {
  Expected::Of<std::string> s = "hello world";

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(matched);
}

TEST(Expected, Exception) {
  Expected::Of<std::string> s = std::make_exception_ptr("error");

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(!matched);
}

TEST(Expected, Unexpected) {
  auto f = [](bool b) -> Expected::Of<std::string> {
    if (b) {
      return Expected("hello");
    } else {
      return Unexpected("error");
    }
  };

  Expected::Of<std::string> s = f(true);

  bool matched = s.Match(
      [](std::string& s) { return true; },
      [](auto) { return false; });

  EXPECT_TRUE(matched);
}

TEST(Expected, Compose) {
  auto f = []() {
    return Expected(41);
  };

  auto e = [&]() {
    return f()
        | Then([](int i) {
             return Expected(i + 1);
           });
  };

  EXPECT_EQ(42, *e());
}

TEST(Expected, NoRaisesDeclarationUnexpected) {
  auto f = []() -> Expected::Of<int> {
    return Unexpected("unexpected");
  };

  auto e = [&]() {
    return f()
        | Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::exception>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<std::runtime_error>(StrEq("unexpected")));
}

TEST(Expected, NoRaisesDeclarationUnexpectedFromDerivedException) {
  struct MyException final : public std::exception {
    const char* what() const noexcept override {
      return "woah";
    }
  };

  auto f = []() -> Expected::Of<int> {
    return Unexpected(MyException());
  };

  auto e = [&]() {
    return f()
        | Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::exception>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<MyException>(StrEq("woah")));
}

TEST(Expected, RaisesDeclarationUnexpectedFromDerivedException) {
  struct MyException final : public std::exception {
    const char* what() const noexcept override {
      return "woah";
    }
  };

  auto f = []() -> Expected::Of<int>::Raises<MyException> {
    return Unexpected(MyException());
  };

  auto e = [&]() {
    return f()
        | Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<MyException>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<MyException>(StrEq("woah")));
}

TEST(Expected, ExpectedNoErrors) {
  auto f = []() {
    return Expected(41);
  };

  auto e = [&]() {
    return f()
        | Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(42, *e());
}

} // namespace
} // namespace eventuals::test
