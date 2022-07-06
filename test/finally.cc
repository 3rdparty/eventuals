#include "eventuals/finally.h"

#include "eventuals/catch.h"
#include "eventuals/eventual.h"
#include "eventuals/expected.h"
#include "eventuals/finally.h"
#include "eventuals/if.h"
#include "eventuals/just.h"
#include "eventuals/promisify.h"
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
        >> Finally([](expected<int, std::exception_ptr>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<int, std::variant<Stopped>> result = *e();

  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(42, *result);
}

TEST(Finally, Fail) {
  auto e = []() {
    return Just(42)
        >> Raise("error")
        >> Finally([](expected<int, std::exception_ptr>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<
      int,
      std::variant<Stopped, std::runtime_error>>
      result = *e();

  ASSERT_FALSE(result.has_value());

  ASSERT_EQ(result.error().index(), 1);

  EXPECT_STREQ(std::get<1>(result.error()).what(), "error");
}

TEST(Finally, Stop) {
  auto e = []() {
    return Eventual<std::string>([](auto& k) {
             k.Stop();
           })
        >> Finally([](expected<std::string, std::exception_ptr>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<
      std::string,
      std::variant<Stopped>>
      result = *e();

  ASSERT_FALSE(result.has_value());

  ASSERT_EQ(result.error().index(), 0);

  EXPECT_STREQ(
      std::get<0>(result.error()).what(),
      "Eventual computation stopped (cancelled)");
}

TEST(Finally, VoidSucceed) {
  auto e = []() {
    return Just()
        >> Finally([](expected<void, std::exception_ptr>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<void, std::variant<Stopped>> result = *e();

  EXPECT_TRUE(result.has_value());
}

TEST(Finally, VoidFail) {
  auto e = []() {
    return Just()
        >> Raise("error")
        >> Finally([](expected<void, std::exception_ptr>&& exception) {
             return Just(std::move(exception));
           });
  };

  expected<void, std::variant<Stopped, std::runtime_error>> result = *e();

  ASSERT_FALSE(result.has_value());

  ASSERT_EQ(result.error().index(), 1);

  EXPECT_STREQ(std::get<1>(result.error()).what(), "error");
}

TEST(Finally, VoidStop) {
  auto e = []() {
    return Eventual<void>([](auto& k) {
             k.Stop();
           })
        >> Finally([](expected<void, std::exception_ptr>&& exception) {
             return Just(std::move(exception));
           });
  };

  expected<void, std::variant<Stopped>> result = *e();

  ASSERT_FALSE(result.has_value());

  ASSERT_EQ(result.error().index(), 0);

  EXPECT_STREQ(
      std::get<0>(result.error()).what(),
      "Eventual computation stopped (cancelled)");
}

TEST(Finally, FinallyInsideThen) {
  auto e = []() {
    return Just(1)
        >> Then([](int status) {
             return Eventual<void>()
                        .raises<std::runtime_error>()
                        .start([](auto& k) {
                          k.Fail(std::runtime_error("error"));
                        })
                 >> Finally([](expected<
                                void,
                                std::variant<
                                    Stopped,
                                    std::runtime_error>>&& e) {
                      return If(e.has_value())
                          .no([e = std::move(e)]() {
                            return Raise(std::move(e.error()))
                                >> Catch()
                                       .raised<std::variant<
                                           Stopped,
                                           std::runtime_error>>(
                                           [](std::variant<
                                               Stopped,
                                               std::runtime_error>&& e) {
                                             ASSERT_EQ(e.index(), 1);
                                             EXPECT_STREQ(
                                                 std::get<1>(e).what(),
                                                 "error");
                                           });
                          })
                          .yes([]() {
                            return Raise("another error");
                          });
                    });
           });
  };

  EXPECT_NO_THROW(*e());
}

} // namespace
} // namespace eventuals::test
