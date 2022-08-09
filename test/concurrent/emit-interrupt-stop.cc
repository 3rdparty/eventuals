#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/interrupt.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

// Tests when when upstream stops after an interrupt the result will
// be stop.
TYPED_TEST(ConcurrentTypedTest, EmitInterruptStop) {
  auto e = [&]() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, auto& handler) {
                 CHECK(handler) << "Test expects interrupt to be registered";
                 if (!handler->Install([&k]() {
                       k.Stop();
                     })) {
                   LOG(FATAL) << "Shouldn't be reached";
                 }
                 k.Begin();
               })
               .next([i = 0](auto& k, auto&) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
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

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
