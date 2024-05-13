#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/range.h"
#include "eventuals/timer.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

TYPED_TEST(ConcurrentTypedTest, Timer) {
  size_t concurrency = 10;

  auto e = [&]() {
    return Range(concurrency)
        >> Map([](int i) {
             // Simulate a latency in getting 'requests'.
             return Timer(std::chrono::milliseconds(50))
                 >> Just(i);
           })
        >> Concurrent([&]() {
             return Map(Let([&](int& i) {
               // Trying to simulate a 'long running eventual' here,
               // that will make 'Concurrent' to create a couple of fibers,
               // that will be used to process.
               return Timer(std::chrono::milliseconds(150))
                   >> Eventual<int>([&](auto& k) {
                        k.Start(42);
                      });
             }));
           })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  this->RunUntil(future);

  auto result = future.get();

  EXPECT_EQ(result.size(), concurrency);
}

} // namespace
} // namespace eventuals::test
