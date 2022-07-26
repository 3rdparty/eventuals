#include "eventuals/type-check.h"

#include <vector>

#include "eventuals/collect.h"
#include "eventuals/iterate.h"
#include "eventuals/promisify.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

TEST(TypeCheck, Lvalue) {
  std::vector<int> v = {5, 12};

  auto s = [&]() {
    return TypeCheck<int&>(Iterate(v))
        | Collect<std::vector<int>>();
  };

  EXPECT_EQ(v, *s());
}


TEST(TypeCheck, Rvalue) {
  auto s = []() {
    return TypeCheck<int>(Iterate(std::vector<int>({5, 12})))
        | Collect<std::vector<int>>();
  };

  EXPECT_EQ(std::vector<int>({5, 12}), *s());
}

} // namespace
} // namespace eventuals::test
