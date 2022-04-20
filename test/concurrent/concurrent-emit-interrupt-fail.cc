#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/interrupt.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "test/concurrent/concurrent.h"
#include "test/expect-throw-what.h"

namespace eventuals {
namespace {
// Tests when when upstream fails after an interrupt the result will
// be fail.
TYPED_TEST(ConcurrentTypedTest, EmitInterruptFail) {
  auto e = [&]() {
    return Stream<int>()
               .raises<std::runtime_error>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&k]() {
                   k.Fail(std::runtime_error("error"));
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

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW_WHAT(future.get(), "error");
}
} // namespace
} // namespace eventuals
