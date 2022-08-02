#include "eventuals/collect.h"

#include <set>
#include <utility>
#include <vector>

#include "eventuals/iterate.h"
#include "eventuals/promisify.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Collect, VectorPass) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        >> Collect<std::vector>();
  };

  std::vector<int> result = *s();

  ASSERT_EQ(result.size(), 2);
  EXPECT_THAT(result, ElementsAre(5, 12));

  // The initial vector should remain unchanged.
  ASSERT_EQ(v.size(), 2);
  EXPECT_THAT(v, ElementsAre(5, 12));
}


TEST(Collect, SetPass) {
  std::set<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        >> Collect<std::set>();
  };

  std::set<int> result = *s();

  ASSERT_EQ(result.size(), 2);
  EXPECT_THAT(result, ElementsAre(5, 12));

  // The initial set should remain unchanged.
  ASSERT_EQ(v.size(), 2);
  EXPECT_THAT(v, ElementsAre(5, 12));
}


TEST(Collect, TypedCollection) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        >> Collect<std::vector<long long>>();
  };

  std::vector<long long> result = *s();

  ASSERT_EQ(result.size(), 2);
  EXPECT_THAT(result, ElementsAre(5, 12));

  // The initial vector should remain unchanged.
  ASSERT_EQ(v.size(), 2);
  EXPECT_THAT(v, ElementsAre(5, 12));
}

} // namespace
} // namespace eventuals::test
