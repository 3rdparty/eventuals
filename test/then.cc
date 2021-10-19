#include "eventuals/then.h"

#include <thread>

#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Terminate;
using eventuals::Then;

using testing::MockFunction;

TEST(ThenTest, Succeed) {
  auto e = [](auto s) {
    return Eventual<std::string>()
        .context(std::move(s))
        .start([](auto& s, auto& k) {
          k.Start(std::move(s));
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .context(1)
               .start([](auto& value, auto& k) {
                 auto thread = std::thread(
                     [&value, &k]() mutable {
                       k.Start(value);
                     });
                 thread.detach();
               })
        | Then([](int i) { return i + 1; })
        | Then([&](auto&& i) {
             return e("then");
           });
  };

  EXPECT_EQ("then", *c());
}


TEST(ThenTest, Fail) {
  auto e = [](auto s) {
    return Eventual<std::string>()
        .context(s)
        .start([](auto& s, auto& k) {
          k.Start(std::move(s));
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 auto thread = std::thread(
                     [&k]() mutable {
                       k.Fail("error");
                     });
                 thread.detach();
               })
        | Then([](int i) { return i + 1; })
        | Then([&](auto&& i) {
             return e("then");
           });
  };

  EXPECT_THROW(*c(), const char*);
}


TEST(ThenTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto) {
    return Eventual<std::string>()
        .start([&](auto&) {
          start.Call();
        })
        .interrupt([](auto& k) {
          k.Stop();
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Start(0);
               })
        | Then([](int i) { return i + 1; })
        | Then([&](auto&& i) {
             return e("then");
           });
  };

  auto [future, k] = Terminate(c());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}
