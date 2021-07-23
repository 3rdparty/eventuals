#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/just.h"
#include "stout/lambda.h"
#include "stout/lock.h"
#include "stout/terminal.h"
#include "stout/then.h"

namespace eventuals = stout::eventuals;

using stout::Callback;

using stout::eventuals::Acquire;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Just;
using stout::eventuals::Lambda;
using stout::eventuals::Lock;
using stout::eventuals::Release;
using stout::eventuals::Synchronizable;
using stout::eventuals::Terminate;
using stout::eventuals::Then;
using stout::eventuals::Wait;

using testing::MockFunction;

TEST(LockTest, Succeed)
{
  Lock lock;

  auto e1 = [&]() {
    return Eventual<std::string>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              eventuals::succeed(k, "t1");
            });
        thread.detach();
      })
      | Acquire(&lock)
      | Lambda([](auto&& value) { return std::move(value); });
  };

  auto e2 = [&]() {
    return Eventual<std::string>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              eventuals::succeed(k, "t2");
            });
        thread.detach();
      })
      | Acquire(&lock)
      | Lambda([](auto&& value) { return std::move(value); });
  };

  auto e3 = [&]() {
    return Release(&lock)
      | Lambda([]() { return "t3"; });
  };

  auto [future1, t1] = Terminate(e1());
  auto [future2, t2] = Terminate(e2());
  auto [future3, t3] = Terminate(e3());

  t1.Start();

  EXPECT_EQ("t1", future1.get());

  t2.Start();

  t3.Start();

  EXPECT_STREQ("t3", future3.get());

  EXPECT_EQ("t2", future2.get());
}


TEST(LockTest, Fail)
{
  Lock lock;

  auto e1 = [&]() {
    return Acquire(&lock)
      | (Eventual<std::string>()
         .start([](auto& k) {
           auto thread = std::thread(
               [&k]() mutable {
                 fail(k, "error");
               });
           thread.detach();
         }))
      | Release(&lock)
      | Lambda([](auto&& value) { return std::move(value); });
  };

  auto e2 = [&]() {
    return Acquire(&lock)
      | Lambda([]() { return "t2"; });
  };

  EXPECT_THROW(*e1(), eventuals::FailedException);

  EXPECT_STREQ("t2", *e2());
}


TEST(LockTest, Stop)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  EXPECT_CALL(start, Call())
    .Times(1);

  Lock lock;

  auto e1 = [&]() {
    return Acquire(&lock)
      | (Eventual<std::string>()
         .start([&](auto& k) {
           start.Call();
         })
         .interrupt([](auto& k) {
           eventuals::stop(k);
         }))
      | Release(&lock);
  };

  auto e2 = [&]() {
    return Acquire(&lock)
      | Lambda([]() { return "t2"; });
  };

  auto [future1, k1] = Terminate(e1());

  Interrupt interrupt;

  k1.Register(interrupt);

  eventuals::start(k1);

  interrupt.Trigger();

  EXPECT_THROW(future1.get(), eventuals::StoppedException);

  EXPECT_STREQ("t2", *e2());
}


TEST(LockTest, Wait)
{
  Lock lock;

  Callback<> callback;

  auto e1 = [&]() {
    return Eventual<std::string>()
      .start([](auto& k) {
        eventuals::succeed(k, "t1");
      })
      | Acquire(&lock)
      | (Wait<std::string>(&lock)
         .context(false)
         .condition([&](auto& waited, auto& k, auto&& value) {
           if (!waited) {
             callback = [&k]() {
               eventuals::notify(k);
             };
             eventuals::wait(k);
             waited = true;
           } else {
             eventuals::succeed(k, value);
           }
         }))
      | Release(&lock);
  };

  auto [future1, t1] = Terminate(e1());

  Interrupt interrupt;

  t1.Register(interrupt);

  t1.Start();

  ASSERT_TRUE(callback);

  Lock::Waiter waiter;

  ASSERT_TRUE(lock.AcquireFast(&waiter));

  callback();

  lock.Release();

  EXPECT_EQ("t1", future1.get());
}


TEST(LockTest, SynchronizableWait)
{
  struct Foo : public Synchronizable
  {
    Foo() : Synchronizable(&lock) {}

    Foo(Foo&& that) : Synchronizable(&lock) {}

    auto Operation()
    {
      return Synchronized(
          Wait<std::string>()
          .condition([](auto& k) {
            auto thread = std::thread(
                [&k]() mutable {
                  eventuals::succeed(k, "operation");
                });
            thread.detach();
          }));
    }

    Lock lock;
  };

  Foo foo;

  Foo foo2 = std::move(foo);

  EXPECT_EQ("operation", *foo2.Operation());
}


TEST(LockTest, SynchronizableThen)
{
  struct Foo : public Synchronizable
  {
    Foo() : Synchronizable(&lock) {}

    auto Operation()
    {
      return Synchronized(
          Then([]() {
            return Just(42);
          }))
        | Lambda([](auto i) {
          return i;
        });
    }

    Lock lock;
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}
