#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "test/concurrent/concurrent.h"
#include "test/expect-throw-what.h"

namespace eventuals {
namespace {
// Tests when an eventuals fails before an eventual succeeds.
TYPED_TEST(ConcurrentTypedTest, FailBeforeStart) {
  Callback<void()> start;
  Callback<void()> fail;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            struct Data {
              void* k;
              int i;
            };
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .raises<std::runtime_error>()
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
                            std::runtime_error("error"));
                      };
                    }
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

  k.Start();

  EXPECT_TRUE(start);
  EXPECT_TRUE(fail);

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  // NOTE: executing 'fail' before 'start'.
  fail();
  start();

  EXPECT_THROW_WHAT(future.get(), "error");
}
} // namespace
} // namespace eventuals
