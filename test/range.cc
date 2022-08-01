#include "eventuals/range.h"

#include <vector>

#include "eventuals/collect.h"
#include "eventuals/promisify.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Range, CommonFlow) {
  auto s = Range(0, 5)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre(0, 1, 2, 3, 4));
}

TEST(Range, IncorrectSetup) {
  auto s = Range(2, 0)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre());
}

TEST(Range, NegativeRange) {
  auto s = Range(-2, 2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre(-2, -1, 0, 1));
}

TEST(Range, NegativeFrom) {
  auto s = Range(-2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre());
}

TEST(Range, DefaultFrom) {
  auto s = Range(3)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre(0, 1, 2));
}

TEST(Range, SpecifiedStep) {
  auto s = Range(0, 10, 2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre(0, 2, 4, 6, 8));
}

TEST(Range, SpecifiedNegativeStep) {
  auto s = Range(10, 0, -2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre(10, 8, 6, 4, 2));
}

TEST(Range, SpecifiedStepIncorrect) {
  auto s = Range(10, 0, 2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre());
}

TEST(Range, SpecifiedStepIncorrectNegative) {
  auto s = Range(0, -10, 2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre());
}

TEST(Range, SpecifiedIncorrect) {
  auto s = Range(0, 10, -2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre());
}

TEST(Range, SpecifiedStepNegative) {
  auto s = Range(0, -10, -2)
      | Collect<std::vector>();

  EXPECT_THAT(*s, ElementsAre(0, -2, -4, -6, -8));
}

} // namespace
} // namespace eventuals::test
