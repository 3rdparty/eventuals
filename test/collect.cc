#include "stout/collect.h"

#include <set>
#include <vector>

#include "gtest/gtest.h"
#include "stout/iterate.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Collect;
using stout::eventuals::Iterate;

TEST(Collect, CommonVectorPass) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::vector<int>>([](int x) {
             return x != 5;
           });
  };

  std::vector<int> res = *s();

  EXPECT_EQ(12, res[0]);
  EXPECT_EQ(1, res.size());
}

TEST(Collect, CommonSetPass) {
  std::set<int> v = {5, 12};

  auto s = [&]() {
    return Iterate(v)
        | Collect<std::set<int>>([](int x) {
             return x != 5;
           });
  };

  std::set<int> res = *s();

  EXPECT_EQ(12, *res.begin());
  EXPECT_EQ(1, res.size());
}