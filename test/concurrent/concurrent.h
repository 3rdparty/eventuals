#pragma once

#include "eventuals/concurrent-ordered.h"
#include "eventuals/concurrent.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"

namespace eventuals::test {

struct ConcurrentType {};
struct ConcurrentOrderedType {};

template <typename Type>
class ConcurrentTypedTest : public EventLoopTest {
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

  template <typename Container>
  auto OrderedOrUnorderedElementsAreArray(const Container& container) {
    if constexpr (std::is_same_v<Type, ConcurrentType>) {
      return testing::UnorderedElementsAreArray(container.begin(), container.end());
    } else {
      return testing::ElementsAreArray(container.begin(), container.end());
    }
  }
};

using ConcurrentTypes = testing::Types<ConcurrentType, ConcurrentOrderedType>;

TYPED_TEST_SUITE(ConcurrentTypedTest, ConcurrentTypes);

} // namespace eventuals::test
