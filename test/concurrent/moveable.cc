#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/interrupt.hh"
#include "eventuals/iterate.hh"
#include "eventuals/let.hh"
#include "eventuals/map.hh"
#include "eventuals/promisify.hh"
#include "test/concurrent/concurrent.hh"

namespace eventuals::test {
namespace {

// Tests that only moveable values will be moved into
// 'Concurrent()' and 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, Moveable) {
  struct Moveable {
    Moveable() = default;
    Moveable(Moveable&&) = default;
  };

  auto e = [&]() {
    return Iterate({Moveable()})
        >> this->ConcurrentOrConcurrentOrdered([]() {
            return Map(Let([](Moveable& moveable) {
              return 42;
            }));
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_THAT(*e(), this->OrderedOrUnorderedElementsAre(42));
}

} // namespace
} // namespace eventuals::test
