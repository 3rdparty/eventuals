#include "eventuals/expected.h"

#include <string>

#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

TEST(Expected, Compose) {
  auto f = []() {
    return expected<int>(40);
  };

  auto e = [&]() {
    return f()
        | Then([](int i) -> expected<int> {
             return tl::expected<int, std::string>(i + 1);
           })
        | Then([](int i) {
             return Just(expected<int>(i));
           })
        | Then([](expected<int> e) {
             CHECK(e.has_value());
             e = tl::expected<int, std::string>(e.value() + 1);
             return e;
           });
  };

  EXPECT_EQ(42, *e());
}

TEST(Expected, NoRaisesDeclarationUnexpected) {
  auto f = []() -> expected<int> {
    return make_unexpected("unexpected");
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
          std::tuple<std::runtime_error>>);

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

  auto f = []() -> expected<int, MyException> {
    return make_unexpected(MyException());
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

TEST(Expected, RaisesDeclarationUnexpectedFromDerivedException) {
  struct MyException final : public std::exception {
    const char* what() const noexcept override {
      return "woah";
    }
  };

  auto f = []() -> expected<int, MyException> {
    return make_unexpected(MyException());
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

} // namespace
} // namespace eventuals::test
