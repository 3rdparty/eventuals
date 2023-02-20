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
        >> Finally([](expected<int, Stopped>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<int, Stopped> result = *e();

  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(42, *result);
}

TEST(Finally, Fail) {
  auto e = []() {
    return Just(42)
        >> Raise("error")
        >> Finally([](expected<
                       int,
                       std::variant<Stopped, std::runtime_error>>&& expected) {
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
        >> Finally([](expected<std::string, Stopped>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<
      std::string,
      Stopped>
      result = *e();

  ASSERT_FALSE(result.has_value());

  EXPECT_STREQ(
      result.error().what(),
      "Eventual computation stopped (cancelled)");
}

TEST(Finally, VoidSucceed) {
  auto e = []() {
    return Just()
        >> Finally([](expected<void, Stopped>&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<void, Stopped> result = *e();

  EXPECT_TRUE(result.has_value());
}

TEST(Finally, VoidFail) {
  auto e = []() {
    return Just()
        >> Raise("error")
        >> Finally([](expected<
                       void,
                       std::variant<
                           Stopped,
                           std::runtime_error>>&& exception) {
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
        >> Finally([](expected<void, Stopped>&& exception) {
             return Just(std::move(exception));
           });
  };

  expected<void, Stopped> result = *e();

  ASSERT_FALSE(result.has_value());

  EXPECT_STREQ(
      result.error().what(),
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
                          .yes([]() {
                            return Raise("another error");
                          })
                          .no([e = std::move(e)]() {
                            CHECK_EQ(e.error().index(), 1);
                            return Raise(std::get<1>(std::move(e.error())))
                                >> Catch()
                                       .raised<std::runtime_error>(
                                           [](std::runtime_error e) {
                                             EXPECT_STREQ(
                                                 e.what(),
                                                 "error");
                                           });
                          });
                    });
           });
  };

  EXPECT_NO_THROW(*e());
}

} // namespace
} // namespace eventuals::test
