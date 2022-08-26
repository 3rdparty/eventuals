#include <string>
#include <vector>

#include "eventuals/callback.hh"
#include "eventuals/collect.hh"
#include "eventuals/eventual.hh"
#include "eventuals/iterate.hh"
#include "eventuals/let.hh"
#include "eventuals/map.hh"
#include "test/concurrent/concurrent.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::test {
namespace {

// Tests when an eventuals stops before an eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, StopBeforeStart) {
  Callback<void()> start;
  Callback<void()> stop;

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
                    if (data.i == 1) {
                      start = [&data]() {
                        static_cast<K*>(data.k)->Start(std::to_string(data.i));
                      };
                    } else {
                      stop = [&data]() {
                        static_cast<K*>(data.k)->Stop();
                      };
                    }
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

  k.Start();

  EXPECT_TRUE(start);
  EXPECT_TRUE(stop);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  // NOTE: executing 'stop' before 'start'.
  stop();
  start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
