#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.hh"
#include "eventuals/eventual.hh"
#include "eventuals/iterate.hh"
#include "eventuals/let.hh"
#include "eventuals/map.hh"
#include "eventuals/reduce.hh"
#include "eventuals/then.hh"
#include "test/concurrent/concurrent.hh"
#include "test/promisify-for-test.hh"

namespace eventuals::test {
namespace {

// Tests what happens when downstream is done before 'Concurrent()' is
// done and each eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, DownstreamDoneBothEventualsSuccess) {
  std::deque<Callback<void()>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .start([&](auto& k) mutable {
                    if (i == 1) {
                      callbacks.emplace_back([&k]() {
                        k.Start("1");
                      });
                    } else {
                      callbacks.emplace_back([&k]() {
                        k.Start("2");
                      });
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
          std::tuple<>>);

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (Callback<void()>& callback : callbacks) {
    callback();
  }

  std::vector<std::string> values = {"1", "2"};

  if constexpr (std::is_same_v<TypeParam, ConcurrentType>) {
    EXPECT_THAT(values, testing::Contains(future.get()));
  } else {
    EXPECT_EQ(values[0], future.get());
  }
}

} // namespace
} // namespace eventuals::test
