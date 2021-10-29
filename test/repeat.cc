#include "eventuals/repeat.h"

#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/reduce.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Acquire;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Lock;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Reduce;
using eventuals::Release;
using eventuals::Repeat;
using eventuals::Terminate;
using eventuals::Then;
using eventuals::Until;

using testing::MockFunction;

TEST(RepeatTest, Succeed) {
  auto e = [](auto i) {
    return Eventual<int>()
        .context(i)
        .start([](auto& i, auto& k) {
          k.Start(std::move(i));
        });
  };

  auto r = [&]() {
    return Repeat(Then([i = 0]() mutable { return i++; }))
        | Until([](auto& i) {
             return i == 5;
           })
        | Map(Then([&](auto&& i) {
             return e(i);
           }))
        | Reduce(
               /* sum = */ 0,
               [](auto& sum) {
                 return Then([&](auto&& i) {
                   sum += i;
                   return true;
                 });
               });
  };

  EXPECT_EQ(10, *r());
}


TEST(RepeatTest, Fail) {
  auto e = [](auto) {
    return Eventual<int>()
        .start([](auto& k) {
          k.Fail("error");
        });
  };

  auto r = [&]() {
    return Repeat(Then([i = 0]() mutable { return i++; }))
        | Until([](auto& i) {
             return i == 5;
           })
        | Map(Then([&](auto&& i) {
             return e(i);
           }))
        | Reduce(
               /* sum = */ 0,
               [](auto& sum) {
                 return Then([&](auto&& i) {
                   sum += i;
                   return true;
                 });
               });
  };

  EXPECT_THROW(*r(), const char*);
}


TEST(RepeatTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto s) {
    return Eventual<int>()
        .interruptible()
        .start([&](auto& k, Interrupt::Handler& handler) {
          handler.Install([&k]() {
            k.Stop();
          });
          start.Call();
        });
  };

  auto r = [&]() {
    return Repeat(Then([i = 0]() mutable { return i++; }))
        | Until([](auto& i) {
             return i == 5;
           })
        | Map(Then([&](auto&& i) {
             return e(i);
           }))
        | Reduce(
               /* sum = */ 0,
               [](auto& sum) {
                 return Then([&](auto&& i) {
                   sum += i;
                   return true;
                 });
               });
  };

  auto [future, k] = Terminate(r());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(RepeatTest, Eventual) {
  auto r = []() {
    return Repeat(
               Eventual<int>()
                   .start([](auto& k) {
                     k.Start(1);
                   }))
        | Loop<int>()
              .context(0)
              .body([](auto&& count, auto& repeated, auto&& value) {
                count += value;
                if (count >= 5) {
                  repeated.Done();
                } else {
                  repeated.Next();
                }
              })
              .ended([](auto& count, auto& k) {
                k.Start(std::move(count));
              });
  };

  EXPECT_EQ(5, *r());
}


TEST(RepeatTest, Map) {
  auto r = []() {
    return Repeat()
        | Map(Eventual<int>()
                  .start([](auto& k) {
                    k.Start(1);
                  }))
        | Loop<int>()
              .context(0)
              .body([](auto&& count, auto& repeated, auto&& value) {
                count += value;
                if (count >= 5) {
                  repeated.Done();
                } else {
                  repeated.Next();
                }
              })
              .ended([](auto& count, auto& k) {
                k.Start(std::move(count));
              });
  };

  EXPECT_EQ(5, *r());
}


TEST(RepeatTest, MapAcquire) {
  Lock lock;

  auto r = [&]() {
    return Repeat(
               Eventual<int>()
                   .start([](auto& k) {
                     k.Start(1);
                   }))
        | Map(
               Acquire(&lock)
               | Then([](auto&& i) {
                   return i;
                 })
               | Release(&lock))
        | Loop<int>()
              .context(0)
              .body([](auto&& count, auto& repeated, auto&& value) {
                count += value;
                if (count >= 5) {
                  repeated.Done();
                } else {
                  repeated.Next();
                }
              })
              .ended([](auto& count, auto& k) {
                k.Start(std::move(count));
              });
  };

  EXPECT_EQ(5, *r());
}
