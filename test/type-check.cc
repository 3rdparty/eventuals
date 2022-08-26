#include "eventuals/type-check.hh"

#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/iterate.hh"
#include "eventuals/promisify.hh"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

TEST(TypeCheck, Lvalue) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return TypeCheck<int&>(Iterate(v))
        >> Collect<std::vector>();
  };

  EXPECT_EQ(v, *s());
}


TEST(TypeCheck, Rvalue) {
  auto s = []() {
    return TypeCheck<int>(Iterate(std::vector<int>({5, 12})))
        >> Collect<std::vector>();
  };

  EXPECT_EQ(std::vector<int>({5, 12}), *s());
}

} // namespace
} // namespace eventuals::test
