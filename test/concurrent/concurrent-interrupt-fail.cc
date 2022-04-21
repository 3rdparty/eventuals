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
#include "eventuals/terminal.h"
#include "test/concurrent/concurrent.h"
#include "test/expect-throw-what.h"

namespace eventuals::test {
namespace {

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case both of the
// eventuals will fail so the result will be a fail.
TYPED_TEST(ConcurrentTypedTest, InterruptFail) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .raises<std::runtime_error>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
                    handler.Install([&k]() {
                      k.Fail(std::runtime_error("error"));
                    });
                    callbacks.emplace_back([]() {});
                  });
            }));
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

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW_WHAT(future.get(), "error");
}

} // namespace
} // namespace eventuals::test
