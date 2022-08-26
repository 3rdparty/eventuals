#include <string>
#include <vector>

// Clang-format automatically places '#include "eventuals/flat-map.hh"'
// at the top of the file.
// This comment prevents this behaviour.
#include "eventuals/collect.hh"
#include "eventuals/flat-map.hh"
#include "eventuals/iterate.hh"
#include "eventuals/promisify.hh"
#include "eventuals/range.hh"
#include "test/concurrent/concurrent.hh"

namespace eventuals::test {
namespace {

// Tests that one can nest 'FlatMap()' within a
// 'Concurrent()' or 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, FlatMap) {
  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([]() {
            return FlatMap([](int i) {
              return Range(i);
            });
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_THAT(*e(), this->OrderedOrUnorderedElementsAre(0, 0, 1));
}

} // namespace
} // namespace eventuals::test
