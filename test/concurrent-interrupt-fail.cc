#include <deque>
#include <string>
#include <vector>

#include "eventuals/callback.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "test/concurrent.h"

using eventuals::Callback;
using eventuals::Collect;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Let;
using eventuals::Map;
using eventuals::Terminate;

// Tests that 'Concurrent()' and 'ConcurrentOrdered()' defers to the
// eventuals on how to handle interrupts and in this case both of the
// eventuals will fail so the result will be a fail.
TYPED_TEST(ConcurrentTypedTest, InterruptFail) {
  std::deque<Callback<>> callbacks;

  auto e = [&]() {
    return Iterate({1, 2})
        | this->ConcurrentOrConcurrentOrdered([&]() {
            return Map(Let([&](int& i) {
              return Eventual<std::string>()
                  .interruptible()
                  .start([&](auto& k, Interrupt::Handler& handler) mutable {
                    handler.Install([&k]() {
                      k.Fail("error");
                    });
                    callbacks.emplace_back([]() {});
                  });
            }));
          })
        | Collect<std::vector<std::string>>();
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  ASSERT_EQ(2, callbacks.size());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), const char*);
}
