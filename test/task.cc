#include "stout/task.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/just.h"
#include "stout/then.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Just;
using stout::eventuals::Task;
using stout::eventuals::Then;

TEST(TaskTest, Succeed) {
  auto e1 = []() -> Task<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  EXPECT_EQ(42, *e1());

  auto e2 = [&]() {
    return e1()
        | Then([](int i) {
             return i + 1;
           })
        | e1();
  };

  EXPECT_EQ(42, *e2());

  auto e3 = []() {
    return Task<int>::With<int, std::string>(
        42,
        "hello world",
        [](auto i, auto s) {
          return Just(i);
        });
  };

  EXPECT_EQ(42, *e3());

  auto e4 = [&]() {
    return e3()
        | Then([](int i) {
             return i + 1;
           })
        | e3();
  };

  EXPECT_EQ(42, *e4());
}
