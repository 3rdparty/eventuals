#include "eventuals/repeat.h"

#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/reduce.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(RepeatTest, Succeed) {
  auto e = [](auto i) {
    return Eventual<int>()
        .context(i)
        .start([](int& i, auto& k) {
          k.Start(std::move(i));
        });
  };

  auto r = [&]() {
    return Repeat([i = 0]() mutable { return i++; })
        >> Until([](int& i) {
             return i == 5;
           })
        >> Map([&](int i) {
             return e(i);
           })
        >> Reduce(
               /* sum = */ 0,
               [](int& sum) {
                 return Then([&](int i) {
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
        .raises<std::runtime_error>()
        .start([](auto& k) {
          k.Fail(std::runtime_error("error"));
        });
  };

  auto r = [&]() {
    return Repeat([i = 0]() mutable { return i++; })
        >> Until([](int& i) {
             return i == 5;
           })
        >> Map([&](int i) {
             return e(i);
           })
        >> Reduce(
               /* sum = */ 0,
               [](int& sum) {
                 return Then([&](int i) {
                   sum += i;
                   return true;
                 });
               });
  };

  EXPECT_THAT(
      [&]() { *r(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}


TEST(RepeatTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto s) {
    return Eventual<int>()
        .interruptible()
        .start([&](auto& k, auto& handler) {
          CHECK(handler) << "Test expects interrupt to be registered";
          handler->Install([&k]() {
            k.Stop();
          });
          start.Call();
        });
  };

  auto r = [&]() {
    return Repeat([i = 0]() mutable { return i++; })
        >> Until([](int& i) {
             return i == 5;
           })
        >> Map([&](int i) {
             return e(i);
           })
        >> Reduce(
               /* sum = */ 0,
               [](int& sum) {
                 return Then([&](int i) {
                   sum += i;
                   return true;
                 });
               });
  };

  auto [future, k] = PromisifyForTest(r());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(RepeatTest, Map) {
  auto r = []() {
    return Repeat()
        >> Map([]() {
             return Eventual<int>()
                 .start([](auto& k) {
                   k.Start(1);
                 });
           })
        >> Loop<int>()
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
    return Repeat([]() {
             return Eventual<int>()
                 .start([](auto& k) {
                   k.Start(1);
                 });
           })
        >> Acquire(&lock)
        >> Map([](int i) {
             return i;
           })
        >> Release(&lock)
        >> Loop<int>()
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


TEST(RepeatTest, StaticHeapSize) {
  auto e = [](auto i) {
    return Eventual<int>()
        .context(i)
        .start([](int& i, auto& k) {
          k.Start(std::move(i));
        });
  };

  auto r = [&]() {
    return Repeat([i = 0]() mutable { return i++; })
        >> Until([](int& i) {
             return i == 5;
           })
        >> Map([&](int i) {
             return e(i);
           })
        >> Reduce(
               /* sum = */ 0,
               [](int& sum) {
                 return Then([&](int i) {
                   sum += i;
                   return true;
                 });
               });
  };

  auto [_, k] = PromisifyForTest(r());

  EXPECT_EQ(0, k.StaticHeapSize().bytes());
}

} // namespace
} // namespace eventuals::test
