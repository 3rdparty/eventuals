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

// Tests when all eventuals are successful.
TEST(CT, Timer) {
  std::deque<Callback<void()>> callbacks;

  std::random_device rd;
  std::mt19937 gen(rd());

  std::uniform_int_distribution<> distribution(50, 500);

  size_t concurrency = 10;
  std::vector<std::string> expected;

  Pipe<int> pipe;

  std::thread t([&]() {
    for (size_t i = 1; i <= concurrency; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      int item = distribution(gen);
      expected.push_back(std::to_string(item));
      *pipe.Write(std::move(item));
    }
    *pipe.Close();
  });

  auto e = [&]() {
    return pipe.Read()
        // >> Map([](auto i) {
        //      return Timer(std::chrono::milliseconds(1500))
        //          >> Just(i);
        //    })
        >> Concurrent([&]() {
             struct Data {
               void* k;
               int i;
             };
             return Map(Let([&](int& i) {
               // return Timer(std::chrono::milliseconds(i / 5))
               // >> Eventual<std::string>(
               return Eventual<std::string>(
                   [&, data = Data()](auto& k) mutable {
                     std::cout << "Actual Function " << i << std::endl;
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

  // static_assert(
  //     eventuals::tuple_types_unordered_equals_v<
  //         typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
  //         std::tuple<RuntimeError>>);

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  t.join();

  // while (callbacks.size() != concurrency) {
  //   this->RunUntilIdle();
  // }

  ASSERT_EQ(concurrency, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (Callback<void()>& callback : callbacks) {
    callback();
  }

  // EXPECT_THAT(future.get(), this->OrderedOrUnorderedElementsAreArray(expected));
  // t.join();
}

} // namespace
} // namespace eventuals::test
