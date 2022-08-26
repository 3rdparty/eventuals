#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.hh"
#include "eventuals/collect.hh"
#include "eventuals/eventual.hh"
#include "eventuals/interrupt.hh"
#include "eventuals/iterate.hh"
#include "eventuals/let.hh"
#include "eventuals/map.hh"
#include "test/concurrent/concurrent.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::test {
namespace {

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case one all of
// the eventuals will stop so the result will be a stop.
TYPED_TEST(ConcurrentTypedTest, InterruptStop) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, auto& handler) mutable {
                    CHECK(handler) << "Test expects interrupt to be registered";
                    handler->Install([&k]() {
                      k.Stop();
                    });
                    callbacks.emplace_back([]() {});
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

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
