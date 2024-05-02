#include <deque>
#include <random>
#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/pipe.h"
#include "eventuals/timer.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

TYPED_TEST(ConcurrentTypedTest, Timer) {
  size_t concurrency = 100;

  Pipe<int> pipe;

  std::thread t([&]() {
    for (size_t i = 1; i <= concurrency; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      *pipe.Write(std::move(i));
    }
    *pipe.Close();
  });

  auto e = [&]() {
    return pipe.Read()
        >> Concurrent([&]() {
             return Map(Let([&](int& i) {
               std::cout << "Done Actual Function " << i << std::endl;
               return Timer(std::chrono::milliseconds(i))
                   >> Eventual<int>([&](auto& k) {
                        k.Start(42);
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

  this->RunUntil(future);

  t.join();

  auto r = future.get();

  std::vector<int> result;
  for (int i = 0; i < concurrency; i++) {
    result.push_back(42);
  }

  EXPECT_EQ(r, result);
}

} // namespace
} // namespace eventuals::test
