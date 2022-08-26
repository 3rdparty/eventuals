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
// eventuals on how to handle interrupts and in this case each eventual
// ignores interrupts so we'll successfully collect all the values.
TYPED_TEST(ConcurrentTypedTest, InterruptSuccess) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>(
                  [&, data = Data()](auto& k) mutable {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    data.i = i;
                    callbacks.emplace_back([&data]() {
                      static_cast<K*>(data.k)->Start(std::to_string(data.i));
                    });
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

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (Callback<void()>& callback : callbacks) {
    callback();
  }

  EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAre("1", "2"));
}

} // namespace
} // namespace eventuals::test
