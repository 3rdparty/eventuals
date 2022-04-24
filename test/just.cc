#include "eventuals/just.h"

#include <thread>

#include "eventuals/just.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

TEST(JustTest, Value) {
  auto e = []() {
    return Just(1);
  };

  EXPECT_EQ(1, *e());
}

TEST(JustTest, Void) {
  bool ran = false;
  auto e = [&]() {
    return Just()
        | Then([&]() {
             ran = true;
           });
  };

  ASSERT_FALSE(ran);
  // A Just<void>() doesn't return anything.
  *e();
  ASSERT_TRUE(ran);
}

TEST(JustTest, Ref) {
  int x = 10;

  auto e = [&]() {
    return Just(std::ref(x))
        | Then([](int& x) {
             x += 100;
           });
  };

  *e();
  EXPECT_EQ(110, x);
}

TEST(JustTest, ConstRef) {
  int x = 10;

  auto e = [&]() {
    return Just(std::cref(x))
        | Then([](const int& x) {
             return std::cref(x);
           });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  x = 42;

  EXPECT_EQ(42, future.get());
}

} // namespace
} // namespace eventuals::test
