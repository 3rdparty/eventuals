#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "test/concurrent.h"

using eventuals::Callback;
using eventuals::Collect;
using eventuals::Eventual;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Map;
using eventuals::Terminate;

// Tests when every eventual either stops or fails.
TYPED_TEST(ConcurrentTypedTest, FailOrStop) {
  std::deque<Callback<>> callbacks;

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
                    callbacks.emplace_back([&data]() {
                      if (data.i == 1) {
                        static_cast<K*>(data.k)->Stop();
                      } else {
                        static_cast<K*>(data.k)->Fail(
                            std::runtime_error("error"));
                      }
                    });
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

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  // NOTE: expecting "any" throwable here depending on whether the
  // eventual that stopped or failed was completed first.
  // Expecting 'StoppedException' for 'ConcurrentOrdered'.
  if constexpr (std::is_same_v<TypeParam, ConcurrentType>) {
    EXPECT_ANY_THROW(future.get());
  } else {
    EXPECT_THROW(future.get(), eventuals::StoppedException);
  }
}
