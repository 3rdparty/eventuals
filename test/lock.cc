#include "eventuals/lock.h"

#include <thread>

#include "eventuals/if.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/map.h"
#include "eventuals/reduce.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

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

  auto [future1, t1] = PromisifyForTest(e1());
  auto [future2, t2] = PromisifyForTest(e2());
  auto [future3, t3] = PromisifyForTest(e3());

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
              .raises<std::runtime_error>()
              .start([](auto& k) {
                auto thread = std::thread(
                    [&k]() mutable {
                      k.Fail(std::runtime_error("error"));
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

  EXPECT_THAT(
      [&]() { *e1(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));

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
              .interruptible()
              .start([&](auto& k, auto& handler) {
                CHECK(handler) << "Test expects interrupt to be registered";
                handler->Install([&k]() {
                  k.Stop();
                });
                start.Call();
              })
        | Release(&lock);
  };

  auto e2 = [&]() {
    return Acquire(&lock)
        | Then([]() { return "t2"; });
  };

  auto [future1, k1] = PromisifyForTest(e1());

  Interrupt interrupt;

  k1.Register(interrupt);

  k1.Start();

  interrupt.Trigger();

  EXPECT_THROW(future1.get(), eventuals::StoppedException);

  EXPECT_STREQ("t2", *e2());
}


TEST(LockTest, Wait) {
  Lock lock;

  Callback<void()> callback;

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

  auto [future1, t1] = PromisifyForTest(e1());

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


TEST(LockTest, SynchronizedMap) {
  struct Foo : public Synchronizable {
    auto Operation() {
      return Iterate({1, 2})
          | Synchronized(Map([](int i) {
               return ++i;
             }))
          | Reduce(
                 /* sum = */ 0,
                 [](auto& sum) {
                   return Then([&](auto i) {
                     sum += i;
                     return true;
                   });
                 });
    }
  };

  Foo foo;

  EXPECT_EQ(5, *foo.Operation());
}


TEST(LockTest, ConditionVariable) {
  struct Foo : public Synchronizable {
    auto WaitFor(int id) {
      return Synchronized(Then([this, id]() {
        auto [iterator, inserted] = condition_variables_.emplace(id, &lock());
        auto& condition_variable = iterator->second;
        return condition_variable.Wait();
      }));
    }

    auto NotifyFor(int id) {
      return Synchronized(Then([this, id]() {
        auto iterator = condition_variables_.find(id);
        return If(iterator == condition_variables_.end())
            .yes([]() {
              return false;
            })
            .no([iterator]() {
              auto& condition_variable = iterator->second;
              condition_variable.Notify();
              return true;
            });
      }));
    }

    auto NotifyAllFor(int id) {
      return Synchronized(Then([this, id]() {
        auto iterator = condition_variables_.find(id);
        return If(iterator == condition_variables_.end())
            .yes([]() {
              return false;
            })
            .no([iterator]() {
              auto& condition_variable = iterator->second;
              condition_variable.NotifyAll();
              return true;
            });
      }));
    }

    std::map<int, ConditionVariable> condition_variables_;
  };

  Foo foo;

  auto [future1, k1] = PromisifyForTest(foo.WaitFor(42));
  auto [future2, k2] = PromisifyForTest(foo.WaitFor(42));
  auto [future3, k3] = PromisifyForTest(foo.WaitFor(42));

  k1.Start();
  k2.Start();
  k3.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future1.wait_for(std::chrono::seconds(0)));

  EXPECT_EQ(
      std::future_status::timeout,
      future2.wait_for(std::chrono::seconds(0)));

  EXPECT_EQ(
      std::future_status::timeout,
      future3.wait_for(std::chrono::seconds(0)));

  EXPECT_FALSE(*foo.NotifyFor(41));

  EXPECT_TRUE(*foo.NotifyFor(42));

  future1.get();

  EXPECT_EQ(
      std::future_status::timeout,
      future2.wait_for(std::chrono::seconds(0)));

  EXPECT_EQ(
      std::future_status::timeout,
      future3.wait_for(std::chrono::seconds(0)));

  EXPECT_TRUE(*foo.NotifyAllFor(42));

  future2.get();
  future3.get();
}

TEST(LockTest, ConditionVariable_UseAfterFree) {
  // A bug was caught in the wild where `ConditionVariable` would enqueue a
  // waiting `eventual` for later notification, even if the waiting condition
  // was already met at the time of creation. This would result in a
  // use-after-free type bug where the `ConditionVariable` at a later point
  // attempts to `notify` a stale eventual.

  struct Foo : public Synchronizable {
    Foo()
      : condition_variable_(&lock()) {}

    auto NotifyAll() {
      return Synchronized(Then([this]() mutable {
        condition_variable_.NotifyAll();
      }));
    }

    auto Wait() {
      return Synchronized(condition_variable_.Wait([]() {
        // Nothing to wait for, carry on.
        return false;
      }));
    }

    ConditionVariable condition_variable_;
  };

  // Create condition object
  Foo foo;

  // `Wait` on an already met condition.
  *foo.Wait();

  // Notify all waiters.
  // There should be none, but if the previous call caused the temporary
  // eventual to be queued up, this will blow up.
  *foo.NotifyAll();
}

} // namespace
} // namespace eventuals::test
