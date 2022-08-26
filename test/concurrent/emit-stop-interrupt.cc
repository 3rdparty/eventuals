#include <string>
#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/eventual.hh"
#include "eventuals/interrupt.hh"
#include "eventuals/let.hh"
#include "eventuals/map.hh"
#include "eventuals/pipe.hh"
#include "eventuals/stream.hh"
#include "test/concurrent/concurrent.hh"
#include "test/promisify-for-test.hh"

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
                 handler->Install([&k]() {
                   k.Stop();
                 });
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
