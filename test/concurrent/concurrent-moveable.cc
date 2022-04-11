#include <vector>

#include "eventuals/collect.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "test/concurrent/concurrent.h"

using eventuals::Collect;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Map;
using eventuals::Terminate;

// Tests that only moveable values will be moved into
// 'Concurrent()' and 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, Moveable) {
  struct Moveable {
    Moveable() = default;
    Moveable(Moveable&&) = default;
  };

  auto e = [&]() {
    return Iterate({Moveable()})
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map(Let([](auto& moveable) {
              return 42;
            }));
          })
        | Collect<std::vector<int>>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre(42));
}
