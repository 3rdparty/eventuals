#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "test/concurrent.h"

using eventuals::Collect;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Let;
using eventuals::Map;
using eventuals::Stream;
using eventuals::Terminate;

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
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Stop();
                 });
                 k.Begin();
               })
               .next([i = 0](auto& k) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
               })
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>([&](auto& k) {
                k.Fail("error");
                interrupt.Trigger();
              });
            }));
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_THROW(future.get(), const char*);
}
