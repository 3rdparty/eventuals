#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/flat-map.h"
#include "eventuals/iterate.h"
#include "eventuals/range.h"
#include "eventuals/terminal.h"
#include "test/concurrent/concurrent.h"

namespace eventuals::test {
namespace {

// Tests that one can nest 'FlatMap()' within a
// 'Concurrent()' or 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, FlatMap) {
  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([]() {
            return FlatMap([](int i) {
              return Range(i);
            });
          })
        | Collect<std::vector<int>>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre(0, 0, 1));
}

} // namespace
} // namespace eventuals::test
