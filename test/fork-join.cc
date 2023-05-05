#include "eventuals/fork-join.h"

#include "eventuals/just.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(ForkJoinTest, UpstreamValue) {
  auto e = []() {
    return Just(std::vector<int>{1, 2, 3, 4})
        >> ForkJoin(
               "ForkJoinTest",
               4,
               [](size_t index, std::vector<int>& v) {
                 return Just(v[index] + 1);
               });
  };

  auto result = *e();

  static_assert(std::is_same_v<decltype(result), std::vector<int>>);

  EXPECT_THAT(result, ElementsAre(2, 3, 4, 5));
}


TEST(ForkJoinTest, UpstreamVoid) {
  auto e = []() {
    return Just()
        >> ForkJoin(
               "ForkJoinTest",
               4,
               [](size_t index) {
                 return Just(index);
               });
  };

  auto result = *e();

  static_assert(std::is_same_v<decltype(result), std::vector<size_t>>);

  EXPECT_THAT(result, ElementsAre(0, 1, 2, 3));
}


TEST(ForkJoinTest, Fail) {
  auto e = []() {
    return ForkJoin(
        "ForkJoinTest",
        4,
        [](size_t index) {
          return Eventual<std::string>()
              .raises<RuntimeError>()
              .start([index](auto& k) {
                if (index == 3) {
                  k.Fail(RuntimeError("error from 3"));
                } else {
                  k.Start(std::to_string(index));
                }
              });
        });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("error from 3")));
}


TEST(ForkJoinTest, Interrupt) {
  auto e = []() {
    return ForkJoin(
        "ForkJoinTest",
        4,
        [](size_t index) {
          return Eventual<std::string>()
              .interruptible()
              .start([index](auto& k, auto& handler) {
                if (index == 3) {
                  CHECK(handler) << "Test expects interrupt to be registered";
                  EXPECT_TRUE(handler->Install([&k]() {
                    k.Stop();
                  }));
                } else {
                  k.Start(std::to_string(index));
                }
              });
        });
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::Stopped);
}

} // namespace
} // namespace eventuals::test
