#include "eventuals/take.h"

#include <vector>

#include "eventuals/collect.h"
#include "eventuals/filter.h"
#include "eventuals/iterate.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Take, IterateTakeLastCollect) {
  std::vector<int> v = {5, 12, 17, 3};

  auto s = [&]() {
    return Iterate(v)
        | TakeLastN(2)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(17, 3));
}

TEST(Take, IterateTakeLastAllCollect) {
  std::vector<int> v = {5, 12, 17, 3};

  auto s = [&]() {
    return Iterate(v)
        | TakeLastN(4)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(5, 12, 17, 3));
}

TEST(Take, IterateTakeRangeCollect) {
  std::vector<int> v = {5, 12, 17, 20, 22, 1, 1, 1};

  auto s = [&]() {
    return Iterate(v)
        | TakeRange(1, 2)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(12, 17));
}

TEST(Take, IterateTakeRangeFilterCollect) {
  std::vector<int> v = {5, 12, 17, 20};

  auto s = [&]() {
    return Iterate(v)
        | TakeRange(1, 2)
        | Filter([](int x) { return x % 2 == 0; })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(12));
}

TEST(Take, IterateTakeFirstCollect) {
  std::vector<int> v = {5, 12, 17, 20};

  auto s = [&]() {
    return Iterate(v)
        | TakeFirstN(3)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(5, 12, 17));
}


TEST(Take, IterateTakeFirstFilterCollect) {
  std::vector<int> v = {5, 12, 17, 21};

  auto s = [&]() {
    return Iterate(v)
        | TakeFirstN(3)
        | Filter([](int x) { return x % 2 == 1; })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(5, 17));
}

TEST(Take, TakeLastOutOfRange) {
  std::vector<int> v = {5, 12, 17, 3};

  auto s = [&]() {
    return Iterate(v)
        | TakeLastN(100)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(5, 12, 17, 3));
}

TEST(Take, TakeFirstOutOfRange) {
  std::vector<int> v = {5, 12, 17, 3};

  auto s = [&]() {
    return Iterate(v)
        | TakeFirstN(100)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(5, 12, 17, 3));
}

TEST(Take, TakeRangeStartOutOfRange) {
  std::vector<int> v = {5, 12, 17, 3};

  auto s = [&]() {
    return Iterate(v)
        | TakeRange(100, 100)
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre());
}

TEST(Take, TakeRangeAmountOutOfRange) {
  std::vector<std::string> v = {"5", "12", "17", "3"};

  auto s = [&]() {
    return Iterate(v)
        | TakeRange(1, 100)
        | Collect<std::vector<std::string>>();
  };

  EXPECT_THAT(*s(), ElementsAre("12", "17", "3"));
}

TEST(Take, UniquePtr) {
  std::vector<std::unique_ptr<int>> v;

  v.emplace_back(std::make_unique<int>(1));
  v.emplace_back(std::make_unique<int>(2));

  auto s = [&]() {
    return Iterate(std::move(v))
        | TakeRange(0, 100)
        | Collect<std::vector<std::unique_ptr<int>>>();
  };

  auto result = *s();

  EXPECT_TRUE(v.empty());

  // NOTE: not using 'ElementsAre()' due to 'std::unique_ptr'.
  ASSERT_EQ(2, result.size());
  EXPECT_EQ(1, *result[0]);
  EXPECT_EQ(2, *result[1]);
}

} // namespace
} // namespace eventuals::test
