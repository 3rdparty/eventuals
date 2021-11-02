#include "eventuals/foreach.h"

#include "eventuals/closure.h"
#include "eventuals/range.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Closure;
using eventuals::Foreach;
using eventuals::Range;
using eventuals::Then;

using testing::ElementsAre;

TEST(Foreach, Test) {
  auto e = []() {
    return Closure([v = std::vector<int>()]() mutable {
      return Foreach(
                 Range(5),
                 [&](int i) {
                   v.push_back(i);
                 })
          | Then([&]() {
               return std::move(v);
             });
    });
  };

  EXPECT_THAT(*e(), ElementsAre(0, 1, 2, 3, 4));
}
