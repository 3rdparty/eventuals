#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case one of the
// eventuals will stop and one will fail so the result will be either
// a fail or stop. 'Fail' for 'ConcurrentOrdered()'.
TYPED_TEST(ConcurrentTypedTest, InterruptFailOrStop) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .raises<std::runtime_error>()
                  .interruptible()
                  .start([&](auto& k, auto& handler) mutable {
                    CHECK(handler) << "Test expects interrupt to be registered";
                    if (i == 1) {
                      EXPECT_TRUE(handler->Install([&k]() {
                        k.Stop();
                      }));
                    } else {
                      EXPECT_TRUE(handler->Install([&k]() {
                        k.Fail(std::runtime_error("error"));
                      }));
                    }
                    callbacks.emplace_back([]() {});
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

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  // NOTE: expecting "any" throwable here depending on whether the
  // eventual that stopped or failed was completed first.
  // Expecting 'std::exception_ptr' for 'ConcurrentOrdered'.
  if constexpr (std::is_same_v<TypeParam, ConcurrentType>) {
    EXPECT_ANY_THROW(future.get());
  } else {
    EXPECT_THAT(
        // NOTE: capturing 'future' as a pointer because until C++20
        // we can't capture a "local binding" by reference and there
        // is a bug with 'EXPECT_THAT' that forces our lambda to be
        // const so if we capture it by copy we can't call 'get()'
        // because that is a non-const function.
        [future = &future]() { future->get(); },
        ThrowsMessage<std::runtime_error>(StrEq("error")));
  }
}

} // namespace
} // namespace eventuals::test
