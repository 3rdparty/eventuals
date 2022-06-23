#include "eventuals/type-check.h"

#include <memory>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/then.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

TEST(TypeCheck, Lvalue) {
  auto s = TypeCheck<int>(Just(4));
  EXPECT_EQ(4, *s);
}

TEST(TypeCheck, Rvalue) {
  auto s = TypeCheck<int>(Iterate(std::vector<int>({5, 12})))
      | Collect<std::vector<int>>();

  EXPECT_EQ(std::vector<int>({5, 12}), *s);
}

TEST(TypeCheck, Ref) {
  int i = 4;
  auto s = TypeCheck<int&>(Then([&]() -> int& { return i; }));
  EXPECT_EQ(4, *s);
}

TEST(TypeCheck, ConstRef) {
  int i = 4;
  auto s = TypeCheck<const int&>(Then([&]() -> const int& { return i; }));
  EXPECT_EQ(4, *s);
}

TEST(TypeCheck, ConstFromNonConstRef) {
  int i = 4;
  auto s = TypeCheck<const int&>(Then([&]() -> int& { return i; }));
  EXPECT_EQ(4, *s);
}

TEST(TypeCheck, Pointer) {
  int i = 4;
  auto s = TypeCheck<int*>(Just(&i));
  EXPECT_EQ(&i, *s);
}

TEST(TypeCheck, ConstPointer) {
  int i = 4;
  auto s = TypeCheck<const int*>(Just(&i));
  EXPECT_EQ(&i, *s);
}

TEST(TypeCheck, ConstPointerFromNonConstPointer) {
  int i = 4;
  auto s = TypeCheck<const int*>(Then([&]() -> int* { return &i; }));
  EXPECT_EQ(&i, *s);
}

TEST(TypeCheck, UniquePtr) {
  auto s = [&]() {
    return TypeCheck<std::unique_ptr<int>>(Just(std::make_unique<int>(4)));
  };
  EXPECT_EQ(4, **s());
}

TEST(TypeCheck, ConstUniquePtr) {
  auto s = [&]() {
    return TypeCheck<std::unique_ptr<const int>>(Just(std::make_unique<const int>(4)));
  };
  EXPECT_EQ(4, **s());
}

TEST(TypeCheck, ConstUniquePtrFromNonConstUniquePtr) {
  auto s = [&]() {
    return TypeCheck<std::unique_ptr<const int>>(Just(std::make_unique<int>(4)));
  };
  EXPECT_EQ(4, **s());
}

// TODO(xander): The whole point of TypeCheck is to *not* compile when types
// don't match. Add non-compilation tests which only pass when invalid type
// pairs correctly cause compilation errors.

} // namespace
} // namespace eventuals::test
