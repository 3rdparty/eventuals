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
        >> Then([](int i) -> expected<int> {
             return tl::expected<int, std::string>(i + 1);
           })
        >> Then([](int i) {
             return Just(expected<int>(i));
           })
        >> Then([](expected<int> e) {
             CHECK(e.has_value());
             e = tl::expected<int, std::string>(e.value() + 1);
             return e;
           });
  };

  EXPECT_EQ(42, *e());
}

TEST(Expected, ComposeStopped) {
  auto f = []() {
    return expected<int, Stopped>(make_unexpected(Stopped()));
  };

  auto e = [&]() {
    return f()
        >> Eventual<int>()
               .start([](auto& k, expected<int, Stopped>&& e) {
                 EXPECT_FALSE(e.has_value());
                 k.Stop();
               });
  };

  EXPECT_THROW(*e(), eventuals::Stopped);
}

TEST(Expected, NoRaisesDeclarationUnexpected) {
  auto f = []() -> expected<int> {
    return make_unexpected("unexpected");
  };

  auto e = [&]() {
    return f()
        >> Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("unexpected")));
}

TEST(Expected, NoRaisesDeclarationUnexpectedFromDerivedException) {
  struct MyError final : public Error {
    const char* what() const noexcept override {
      return "woah";
    }
  };

  auto f = []() -> expected<int, MyError> {
    return make_unexpected(MyError());
  };

  auto e = [&]() {
    return f()
        >> Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<MyError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<MyError>(StrEq("woah")));
}

TEST(Expected, RaisesDeclarationUnexpectedFromDerivedException) {
  struct MyError final : public Error {
    const char* what() const noexcept override {
      return "woah";
    }
  };

  auto f = []() -> expected<int, MyError> {
    return make_unexpected(MyError());
  };

  auto e = [&]() {
    return f()
        >> Then([](int i) {
             return i + 1;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<MyError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<MyError>(StrEq("woah")));
}

} // namespace
} // namespace eventuals::test
