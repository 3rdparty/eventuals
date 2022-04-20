#pragma once

#include "eventuals/concurrent-ordered.h"
#include "eventuals/concurrent.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {

struct ConcurrentType {};
struct ConcurrentOrderedType {};

template <typename Type>
class ConcurrentTypedTest : public testing::Test {
 public:
  template <typename F>
  auto ConcurrentOrConcurrentOrdered(F f) {
    if constexpr (std::is_same_v<Type, ConcurrentType>) {
      return eventuals::Concurrent(std::move(f));
    } else {
      return eventuals::ConcurrentOrdered(std::move(f));
    }
  }

  template <typename... Args>
  auto OrderedOrUnorderedElementsAre(Args&&... args) {
    if constexpr (std::is_same_v<Type, ConcurrentType>) {
      return testing::UnorderedElementsAre(std::forward<Args>(args)...);
    } else {
      return testing::ElementsAre(std::forward<Args>(args)...);
    }
  }
};

using ConcurrentTypes = testing::Types<ConcurrentType, ConcurrentOrderedType>;

TYPED_TEST_SUITE(ConcurrentTypedTest, ConcurrentTypes);

} // namespace eventuals::test
