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
#include "test/expect-throw-what.h"

using eventuals::Callback;
using eventuals::Collect;
using eventuals::Eventual;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Map;
using eventuals::Terminate;

// Tests when at least one of the eventuals fails.
TYPED_TEST(ConcurrentTypedTest, Fail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
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
                      if (data.i == 1) {
                        static_cast<K*>(data.k)->Start(std::to_string(data.i));
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

  auto [future, k] = Terminate(e());

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  for (auto& callback : callbacks) {
    callback();
  }

  EXPECT_THROW_WHAT(future.get(), "error");
}
