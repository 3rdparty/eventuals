#include "stout/lock.h"

#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/just.h"
#include "stout/terminal.h"
#include "stout/then.h"

namespace eventuals = stout::eventuals;

using stout::Callback;

using stout::eventuals::Acquire;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Just;
using stout::eventuals::Lock;
using stout::eventuals::Release;
using stout::eventuals::Scheduler;
using stout::eventuals::Synchronizable;
using stout::eventuals::Terminate;
using stout::eventuals::Then;
using stout::eventuals::Wait;

using testing::MockFunction;

TEST(LockTest, Succeed) {
  Lock lock;

  auto e1 = [&]() {
    return Eventual<std::string>()
               .start([](auto& k) {
                 auto thread = std::thread(
                     [&k]() mutable {
                       k.Start("t1");
                     });
                 thread.detach();
               })
        | Acquire(&lock)
        | Then([](auto&& value) { return std::move(value); });
  };

  auto e2 = [&]() {
    return Eventual<std::string>()
               .start([](auto& k) {
                 auto thread = std::thread(
                     [&k]() mutable {
                       k.Start("t2");
                     });
                 thread.detach();
               })
        | Acquire(&lock)
        | Then([](auto&& value) { return std::move(value); });
  };

  auto e3 = [&]() {
    return Release(&lock)
        | Then([]() { return "t3"; });
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


TEST(LockTest, Fail) {
  Lock lock;

  auto e1 = [&]() {
    return Acquire(&lock)
        | Eventual<std::string>()
              .start([](auto& k) {
                auto thread = std::thread(
                    [&k]() mutable {
                      k.Fail("error");
                    });
                thread.detach();
              })
        | Release(&lock)
        | Then([](auto&& value) { return std::move(value); });
  };

  auto e2 = [&]() {
    return Acquire(&lock)
        | Then([]() { return "t2"; });
  };

  EXPECT_THROW(*e1(), const char*);

  EXPECT_STREQ("t2", *e2());
}


TEST(LockTest, Stop) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  EXPECT_CALL(start, Call())
      .Times(1);

  Lock lock;

  auto e1 = [&]() {
    return Acquire(&lock)
        | Eventual<std::string>()
              .start([&](auto& k) {
                start.Call();
              })
              .interrupt([](auto& k) {
                k.Stop();
              })
        | Release(&lock);
  };

  auto e2 = [&]() {
    return Acquire(&lock)
        | Then([]() { return "t2"; });
  };

  auto [future1, k1] = Terminate(e1());

  Interrupt interrupt;

  k1.Register(interrupt);

  k1.Start();

  interrupt.Trigger();

  EXPECT_THROW(future1.get(), eventuals::StoppedException);

  EXPECT_STREQ("t2", *e2());
}


TEST(LockTest, Wait) {
  Lock lock;

  Callback<> callback;

  auto e1 = [&]() {
    return Eventual<std::string>()
               .start([](auto& k) {
                 k.Start("t1");
               })
        | Acquire(&lock)
        | Wait(&lock,
               [&](auto notify) {
                 callback = std::move(notify);
                 return [waited = false](auto&& value) mutable {
                   if (!waited) {
                     waited = true;
                     return true;
                   } else {
                     return false;
                   }
                 };
               })
        | Release(&lock);
  };

  auto [future1, t1] = Terminate(e1());

  Interrupt interrupt;

  t1.Register(interrupt);

  t1.Start();

  ASSERT_TRUE(callback);

  Lock::Waiter waiter;
  waiter.context = Scheduler::Context::Get();

  ASSERT_TRUE(lock.AcquireFast(&waiter));

  callback();

  lock.Release();

  EXPECT_EQ("t1", future1.get());
}


TEST(LockTest, SynchronizableWait) {
  struct Foo : public Synchronizable {
    Foo() {}

    Foo(Foo&& that)
      : Synchronizable() {}

    auto Operation() {
      return Synchronized(
          Just("operation")
          | Wait([](auto notify) {
              return [](auto&&...) {
                return false;
              };
            }));
    }
  };

  Foo foo;

  Foo foo2 = std::move(foo);

  EXPECT_EQ("operation", *foo2.Operation());
}


TEST(LockTest, SynchronizableThen) {
  struct Foo : public Synchronizable {
    auto Operation() {
      return Synchronized(
                 Then([]() {
                   return Just(42);
                 }))
          | Then([](auto i) {
               return i;
             });
    }
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}


TEST(LockTest, OwnedByCurrentSchedulerContext) {
  struct Foo : public Synchronizable {
    auto Operation() {
      return Synchronized(
                 Then([this]() {
                   if (!lock().OwnedByCurrentSchedulerContext()) {
                     ADD_FAILURE() << "lock should be owned";
                   }
                   return Just(42);
                 }))
          | Then([this](auto i) {
               if (lock().OwnedByCurrentSchedulerContext()) {
                 ADD_FAILURE() << "lock should not be owned";
               }
               return i;
             });
    }
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}
