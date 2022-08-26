#include <string>
#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/map.hh"
#include "eventuals/promisify.hh"
#include "eventuals/stream.hh"
#include "test/concurrent/concurrent.hh"

namespace eventuals::test {
namespace {

// Tests when when upstream stops the result will be stop.
TYPED_TEST(ConcurrentTypedTest, StreamStop) {
  auto e = [&]() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Stop();
               })
        >> this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_THROW(*e(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
