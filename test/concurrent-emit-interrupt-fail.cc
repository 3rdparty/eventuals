#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/interrupt.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "test/concurrent.h"

using eventuals::Collect;
using eventuals::Interrupt;
using eventuals::Map;
using eventuals::Stream;
using eventuals::Terminate;

// Tests when when upstream fails after an interrupt the result will
// be fail.
TYPED_TEST(ConcurrentTypedTest, EmitInterruptFail) {
  auto e = [&]() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Fail("error");
                 });
                 k.Begin();
               })
               .next([i = 0](auto& k) mutable {
                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 }
               })
        | this->ConcurrentOrConcurrentOrdered([]() {
            return Map([](int i) {
              return std::to_string(i);
            });
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), std::exception_ptr);
}