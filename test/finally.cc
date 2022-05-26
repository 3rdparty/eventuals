#include "eventuals/finally.h"

#include "eventuals/catch.h"
#include "eventuals/eventual.h"
#include "eventuals/finally.h"
#include "eventuals/if.h"
#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

TEST(Finally, Succeed) {
  auto e = []() {
    return Just(42)
        | Finally([](auto&& expected) {
             return Just(std::move(expected));
           });
  };

  auto expected = *e();

  static_assert(std::is_same_v<Expected::Of<int>, decltype(expected)>);

  ASSERT_TRUE(expected);

  EXPECT_EQ(42, *expected);
}

TEST(Finally, Fail) {
  auto e = []() {
    return Just(42)
        | Raise("error")
        | Finally([](auto&& expected) {
             return Just(std::move(expected));
           });
  };

  auto expected = *e();

  static_assert(std::is_same_v<Expected::Of<int>, decltype(expected)>);

  EXPECT_THAT(
      [&]() { *expected; },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

TEST(Finally, Stop) {
  auto e = []() {
    return Eventual<std::string>([](auto& k) {
             k.Stop();
           })
        | Finally([](auto&& expected) {
             return Just(std::move(expected));
           });
  };

  auto expected = *e();

  static_assert(std::is_same_v<Expected::Of<std::string>, decltype(expected)>);

  EXPECT_THROW(*expected, eventuals::StoppedException);
}

TEST(Finally, VoidSucceed) {
  auto e = []() {
    return Just()
        | Finally([](auto&& exception) {
             return Just(std::move(exception));
           });
  };

  auto exception = *e();

  static_assert(
      std::is_same_v<
          std::optional<std::exception_ptr>,
          decltype(exception)>);

  EXPECT_FALSE(exception.has_value());
}

TEST(Finally, VoidFail) {
  auto e = []() {
    return Just()
        | Raise("error")
        | Finally([](auto&& exception) {
             return Just(std::move(exception));
           });
  };

  auto exception = *e();

  static_assert(
      std::is_same_v<
          std::optional<std::exception_ptr>,
          decltype(exception)>);

  ASSERT_TRUE(exception.has_value());

  EXPECT_THAT(
      [&]() { std::rethrow_exception(exception.value()); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

TEST(Finally, VoidStop) {
  auto e = []() {
    return Eventual<void>([](auto& k) {
             k.Stop();
           })
        | Finally([](auto&& exception) {
             return Just(std::move(exception));
           });
  };

  auto exception = *e();

  static_assert(
      std::is_same_v<
          std::optional<std::exception_ptr>,
          decltype(exception)>);

  ASSERT_TRUE(exception.has_value());

  EXPECT_THROW(
      std::rethrow_exception(exception.value()),
      eventuals::StoppedException);
}

TEST(Finally, FinallyInsideThen) {
  auto e = [&]() {
    return Just(1)
        | Then([](int status) {
             return Eventual<void>()
                        .raises<std::runtime_error>()
                        .start([](auto& k) {
                          k.Fail(std::runtime_error("error"));
                        })
                 | Finally([](std::optional<std::exception_ptr>&& error) {
                      return If(error.has_value())
                          .yes([error = std::move(error)]() {
                            return Raise(std::move(error.value()))
                                | Catch()
                                      .raised<std::exception>(
                                          [](std::exception&& error) {
                                            EXPECT_STREQ(
                                                error.what(),
                                                "error");
                                          });
                          })
                          .no([]() {
                            return Raise("another error");
                          });
                    });
           });
  };

  EXPECT_NO_THROW(*e());
}

} // namespace
} // namespace eventuals::test
