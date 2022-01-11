#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/flat-map.h"
#include "eventuals/iterate.h"
#include "eventuals/range.h"
#include "eventuals/terminal.h"
#include "test/concurrent.h"

using eventuals::Collect;
using eventuals::FlatMap;
using eventuals::Iterate;
using eventuals::Range;
using eventuals::Terminate;

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

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre(0, 0, 1));
}
