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
        >> Finally([](auto&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<int, std::exception_ptr> result = *e();

  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(42, *result);
}

TEST(Finally, Fail) {
  auto e = []() {
    return Just(42)
        >> Raise("error")
        >> Finally([](auto&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<int, std::exception_ptr> result = *e();

  ASSERT_FALSE(result.has_value());

  EXPECT_THAT(
      [&]() { std::rethrow_exception(result.error()); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

TEST(Finally, Stop) {
  auto e = []() {
    return Eventual<std::string>([](auto& k) {
             k.Stop();
           })
        >> Finally([](auto&& expected) {
             return Just(std::move(expected));
           });
  };

  expected<std::string, std::exception_ptr> result = *e();

  ASSERT_FALSE(result.has_value());

  EXPECT_THROW(
      std::rethrow_exception(result.error()),
      eventuals::StoppedException);
}

TEST(Finally, VoidSucceed) {
  auto e = []() {
    return Just()
        >> Finally([](auto&& exception) {
             return Just(std::move(exception));
           });
  };

  expected<void, std::exception_ptr> result = *e();

  EXPECT_TRUE(result.has_value());
}

TEST(Finally, VoidFail) {
  auto e = []() {
    return Just()
        >> Raise("error")
        >> Finally([](auto&& exception) {
             return Just(std::move(exception));
           });
  };

  expected<void, std::exception_ptr> result = *e();

  ASSERT_FALSE(result.has_value());

  EXPECT_THAT(
      [&]() { std::rethrow_exception(result.error()); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

TEST(Finally, VoidStop) {
  auto e = []() {
    return Eventual<void>([](auto& k) {
             k.Stop();
           })
        >> Finally([](auto&& exception) {
             return Just(std::move(exception));
           });
  };

  expected<void, std::exception_ptr> result = *e();

  ASSERT_FALSE(result.has_value());

  EXPECT_THROW(
      std::rethrow_exception(result.error()),
      eventuals::StoppedException);
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
                 >> Finally([](expected<void, std::exception_ptr>&& e) {
                      return If(e.has_value())
                          .no([e = std::move(e)]() {
                            return Raise(std::move(e.error()))
                                >> Catch()
                                       .raised<std::exception>(
                                           [](std::exception&& e) {
                                             EXPECT_STREQ(
                                                 e.what(),
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
