#include <deque>
#include <string>

#include "eventuals/callback.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/reduce.h"
#include "eventuals/then.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

// Tests what happens when downstream is done before 'Concurrent()' is
// done and one eventual fails.
TYPED_TEST(ConcurrentTypedTest, DownstreamDoneOneEventualFail) {
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
                      callbacks.emplace_back([&k]() {
                        k.Start("1");
                      });
                    } else {
                      EXPECT_TRUE(handler->Install([&k]() {
                        k.Fail(std::runtime_error("error"));
                      }));
                      callbacks.emplace_back([]() {});
                    }
                  });
            }));
          })
        >> Reduce(
               std::string(),
               [](std::string& result) {
                 return Then([&](std::string&& value) {
                   result = value;
                   return false; // Only take the first element!
                 });
               });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (Callback<void()>& callback : callbacks) {
    callback();
  }

  EXPECT_EQ("1", future.get());
}

} // namespace
} // namespace eventuals::test
