#include "eventuals/eventual.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "promisify-for-test.h"

namespace eventuals::test {
namespace {

TEST(BitwiseOperatorTest, Succeed) {
  struct Movable {
    Movable() = default;
    Movable(Movable&& that) {
      that.moved = true;
    }

    bool moved = false;
  } movable;

  auto check_movable = [](const Movable& movable) {
    EXPECT_FALSE(movable.moved);
    return Just();
  };

  auto e = [&]() {
    return check_movable(movable)
        >> Then([&, movable = std::move(movable)]() {
             check_movable(movable);
           })
        >> Then([&]() {
             EXPECT_TRUE(movable.moved);
           });
  };

  *e();
}

} // namespace
} // namespace eventuals::test
