#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "gmock/gmock.h"
#include "test/concurrent/concurrent.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::StrEq;
using testing::ThrowsMessage;

// Tests when an eventuals fails before an eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, FailBeforeStart) {
  Callback<void()> start;
  Callback<void()> fail;

  auto e = [&]() {
    return Iterate({1, 2})
        >> this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .raises<RuntimeError>()
                  .start([&, data = Data()](auto& k) mutable {
                    using K = std::decay_t<decltype(k)>;
                    data.k = &k;
                    data.i = i;
                    if (data.i == 1) {
                      start = [&data]() {
                        static_cast<K*>(data.k)->Start(std::to_string(data.i));
                      };
                    } else {
                      fail = [&data]() {
                        static_cast<K*>(data.k)->Fail(
                            RuntimeError("error"));
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
          std::tuple<RuntimeError>>);


  auto [future, k] = PromisifyForTest(e());

  k.Start();

  EXPECT_TRUE(start);
  EXPECT_TRUE(fail);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  // NOTE: executing 'fail' before 'start'.
  fail();
  start();

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<RuntimeError>(StrEq("error")));
}

} // namespace
} // namespace eventuals::test
