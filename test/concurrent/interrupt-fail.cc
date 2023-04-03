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

using testing::ThrowsMessage;

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case both of the
// eventuals will fail so the result will be a fail.
TYPED_TEST(ConcurrentTypedTest, InterruptFail) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .raises<RuntimeError>()
                  .interruptible()
                  .start([&](auto& k, auto& handler) mutable {
                    CHECK(handler) << "Test expects interrupt to be registered";
                    EXPECT_TRUE(handler->Install([&k]() {
                      k.Fail(RuntimeError("error"));
                    }));
                    callbacks.emplace_back([]() {});
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

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  try {
    future.get();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

} // namespace
} // namespace eventuals::test
