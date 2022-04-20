#include "eventuals/filter.h"

#include <set>
#include <unordered_set>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/iterate.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::UnorderedElementsAre;

TEST(Filter, OddLoopFlow) {
  std::vector<int> v = {5, 12, 17};
  auto begin = v.begin();
  auto end = v.end();

  auto s = [&]() {
    return Iterate(begin, end)
        | Filter([](int x) {
             return x % 2 == 1;
           })
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(22, *s());
}

TEST(Filter, OddCollectFlow) {
  std::vector<int> v = {5, 12, 17};
  auto begin = v.begin();
  auto end = v.end();

  auto s = [&]() {
    return Iterate(begin, end)
        | Filter([](int x) { return x % 2 == 1; })
        | Collect<std::set<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(5, 17));
}

TEST(Filter, OddMapLoopFlow) {
  std::vector<int> v = {5, 12, 17};

  auto s = [&]() {
    return Iterate(v)
        | Filter([](int x) { return x % 2 == 1; })
        | Map([](int x) { return x + 1; })
        | Loop<int>()
              .context(0)
              .body([](auto& sum, auto& stream, auto&& value) {
                sum += value;
                stream.Next();
              })
              .ended([](auto& sum, auto& k) {
                k.Start(sum);
              });
  };

  EXPECT_EQ(24, *s());
}

TEST(Filter, OddMapCollectFlow) {
  std::vector<int> v = {5, 12, 17};

  auto s = [&]() {
    return Iterate(v)
        | Filter([](int x) { return x % 2 == 1; })
        | Map([](int x) { return x + 1; })
        | Collect<std::unordered_set<int>>();
  };

  EXPECT_THAT(*s(), UnorderedElementsAre(6, 18));
}

} // namespace
} // namespace eventuals::test
