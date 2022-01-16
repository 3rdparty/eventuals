#include "eventuals/if.h"

#include <thread>

#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using std::string;

using eventuals::Eventual;
using eventuals::If;
using eventuals::Interrupt;
using eventuals::Just;
using eventuals::Raise;
using eventuals::Terminate;
using eventuals::Then;

using testing::MockFunction;

TEST(IfTest, Then) {
  auto e = []() {
    return Just(1)
        | Then([](int i) {
             return If(i == 1)
                 .then(Just("then"))
                 .otherwise(Just("otherwise"));
           });
  };

  EXPECT_EQ("then", *e());
}


TEST(IfTest, Otherwise) {
  auto e = []() {
    return Just(0)
        | Then([](int i) {
             return If(i == 1)
                 .then(Just("then"))
                 .otherwise(Just("otherwise"));
           });
  };

  EXPECT_EQ("otherwise", *e());
}


TEST(IfTest, Fail) {
  auto e = []() {
    return Just(0)
        | Raise("error")
        | Then([](int i) {
             return If(i == 1)
                 .then(Just("then"))
                 .otherwise(Just("otherwise"));
           });
  };

  EXPECT_THROW(*e(), const char*);
}


TEST(IfTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&]() {
    return Just(1)
        | Then([&](int i) {
             return If(i == 1)
                 .then(Eventual<const char*>()
                           .interruptible()
                           .start([&](auto& k, Interrupt::Handler& handler) {
                             handler.Install([&k]() {
                               k.Stop();
                             });
                             start.Call();
                           }))
                 .otherwise(Just("otherwise"));
           });
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(IfTest, Raise) {
  auto e = []() {
    return Just(1)
        | Then([](int i) {
             return If(i == 1)
                 .then(Just(i))
                 .otherwise(Raise("raise"));
           });
  };

  EXPECT_EQ(1, *e());
}
