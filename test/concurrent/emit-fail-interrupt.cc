#include <string>
#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/eventual.hh"
#include "eventuals/interrupt.hh"
#include "eventuals/let.hh"
#include "eventuals/map.hh"
#include "eventuals/pipe.hh"
#include "eventuals/stream.hh"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

// Tests that when one of the 'Concurrent()' eventuals fails it can
// ensure that everything correctly fails by "interrupting"
// upstream. In this case we interrupt upstream by using an
// 'Interrupt' but there may diffrent ways of doing it depending on
// what you're building. See the TODO in
// '_Concurrent::TypeErasedAdaptor::Done()' for more details on the
// semantics of 'Concurrent()' that are important to consider here.
TYPED_TEST(ConcurrentTypedTest, EmitFailInterrupt) {
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
              return Eventual<std::string>()
                  .raises<std::runtime_error>()
                  .start([&](auto& k) {
                    k.Fail(std::runtime_error("error"));
                    interrupt.Trigger();
                  });
            }));
          })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);


  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

} // namespace
} // namespace eventuals::test
