#include "eventuals/collect.h"

#include <set>
#include <utility>
#include <vector>

#include "eventuals/iterate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Collect, VectorPass) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::vector<int>>();
  };

  std::vector<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_THAT(v, ElementsAre(5, 12));

  // The initial vector should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_THAT(v, ElementsAre(5, 12));
}


TEST(Collect, SetPass) {
  std::set<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::set<int>>();
  };

  std::set<int> result = *s();

  ASSERT_EQ(2, result.size());
  EXPECT_THAT(v, ElementsAre(5, 12));

  // The initial set should remain unchanged.
  ASSERT_EQ(2, v.size());
  EXPECT_THAT(v, ElementsAre(5, 12));
}

} // namespace
} // namespace eventuals::test
