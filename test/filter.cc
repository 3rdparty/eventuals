#include "stout/filter.h"

#include <set>
#include <unordered_set>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/collect.h"
#include "stout/iterate.h"
#include "stout/loop.h"
#include "stout/map.h"
#include "stout/terminal.h"
#include "stout/then.h"

using stout::eventuals::Collect;
using stout::eventuals::Filter;
using stout::eventuals::Iterate;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Then;
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
        | Map(Then([](int x) { return x + 1; }))
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
        | Map(Then([](int x) { return x + 1; }))
        | Collect<std::unordered_set<int>>();
  };

  EXPECT_THAT(*s(), UnorderedElementsAre(6, 18));
}