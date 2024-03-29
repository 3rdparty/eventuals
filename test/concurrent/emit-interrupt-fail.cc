#include <string>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/interrupt.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

// Tests when when upstream fails after an interrupt the result will
// be fail.
TYPED_TEST(ConcurrentTypedTest, EmitInterruptFail) {
  auto e = [&]() {
    return Stream<int>()
               .raises<RuntimeError>()
               .interruptible()
               .begin([](auto& k, auto& handler) {
                 CHECK(handler) << "Test expects interrupt to be registered";
                 k.Begin();
               })
               .next([i = 0](auto& k, auto& handler) mutable {
                 CHECK(handler) << "Test expects interrupt to be registered";

                 i++;
                 if (i == 1) {
                   k.Emit(i);
                 } else {
                   EXPECT_TRUE(handler->Install([&k]() {
                     k.Fail(RuntimeError("error"));
                   }));
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
          std::tuple<RuntimeError>>);

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<RuntimeError>(StrEq("error")));
}

} // namespace
} // namespace eventuals::test
