#include "stout/filter.h"

#include <set>
#include <unordered_set>
#include <vector>

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

  auto res = *s();

  ASSERT_EQ(res.size(), 2);
  EXPECT_EQ(*res.begin(), 5);
  EXPECT_EQ(*(++res.begin()), 17);
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

// NOTE: Works on Linux, MacOS.
// TODO(onelxj): Fix this test for Windows.
// *res.begin() returns 6.
// *++res.begin() returns 18.
// TEST(Filter, OddMapCollectFlow) {
//   std::vector<int> v = {5, 12, 17};
//
//   auto s = [&]() {
//     return Iterate(v)
//         | Filter([](int x) { return x % 2 == 1; })
//         | Map(Then([](int x) { return x + 1; }))
//         | Collect<std::unordered_set<int>>();
//   };
//
//   auto res = *s();
//
//   ASSERT_EQ(res.size(), 2);
//   EXPECT_EQ(*res.begin(), 18);
//   EXPECT_EQ(*++res.begin(), 6);
// }