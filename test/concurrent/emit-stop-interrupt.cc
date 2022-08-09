#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

// Same as 'EmitFailInterrupt' except each eventual stops not fails.
TYPED_TEST(ConcurrentTypedTest, EmitStopInterrupt) {
  Interrupt interrupt;

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
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>([&](auto& k) {
                k.Stop();
                interrupt.Trigger();
              });
            }));
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
