#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/timer.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

// Tests when all eventuals are successful.
TYPED_TEST(ConcurrentTypedTest, Success) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              std::cout << "setting up timer for " << i << std::endl;
              return Timer(std::chrono::milliseconds(i == 2 ? 10 : 100))
                  >> Eventual<std::string>(
                         [&, data = Data()](auto& k) mutable {
                           std::cout << "executing eventual for " << i << std::endl;
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
          std::tuple<RuntimeError>>);

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  while (callbacks.size() != 2) {
    this->RunUntilIdle();
  }

  ASSERT_EQ(2, callbacks.size());

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
