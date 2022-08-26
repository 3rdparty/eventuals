#include "eventuals/foreach.hh"

#include "eventuals/closure.hh"
#include "eventuals/promisify.hh"
#include "eventuals/range.hh"
#include "eventuals/then.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Foreach, Test) {
  auto e = []() {
    return Closure([v = std::vector<int>()]() mutable {
      return Foreach(
                 Range(5),
                 [&](int i) {
                   v.push_back(i);
                 })
          >> Then([&]() {
               return std::move(v);
             });
    });
  };

  EXPECT_THAT(*e(), ElementsAre(0, 1, 2, 3, 4));
}

} // namespace
} // namespace eventuals::test
