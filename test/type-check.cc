#include "eventuals/type-check.h"

#include <vector>

#include "eventuals/collect.h"
#include "eventuals/iterate.h"
#include "eventuals/terminal.h"
#include "gtest/gtest.h"

using eventuals::Collect;
using eventuals::Iterate;
using eventuals::TypeCheck;

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
