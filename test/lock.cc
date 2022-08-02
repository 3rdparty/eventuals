#include "eventuals/lock.h"

#include <optional>
#include <thread>

#include "eventuals/if.h"
#include "eventuals/interrupt.h"
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
                 std::thread thread(
                     [&k]() mutable {
                       k.Start("t1");
                     });
                 thread.detach();
               })
        >> Acquire(&lock)
        >> Then([](std::string&& value) { return std::move(value); });
  };

  auto e2 = [&]() {
    return Eventual<std::string>()
               .start([](auto& k) {
                 std::thread thread(
                     [&k]() mutable {
                       k.Start("t2");
                     });
                 thread.detach();
               })
        >> Acquire(&lock)
        >> Then([](std::string&& value) { return std::move(value); });
  };

  auto e3 = [&]() {
    return Release(&lock)
        >> Then([]() { return "t3"; });
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
        >> Eventual<std::string>()
               .raises<std::runtime_error>()
               .start([](auto& k) {
                 std::thread thread(
                     [&k]() mutable {
                       k.Fail(std::runtime_error("error"));
                     });
                 thread.detach();
               })
        >> Release(&lock)
        >> Then([](std::string&& value) { return std::move(value); });
  };

  auto e2 = [&]() {
    return Acquire(&lock)
        >> Then([]() { return "t2"; });
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
        >> Eventual<std::string>()
               .interruptible()
               .start([&](auto& k, std::optional<Interrupt::Handler>& handler) {
                 CHECK(handler) << "Test expects interrupt to be registered";
                 handler->Install([&k]() {
                   k.Stop();
                 });
                 start.Call();
               })
        >> Release(&lock);
  };

  auto e2 = [&]() {
    return Acquire(&lock)
        >> Then([]() { return "t2"; });
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

  int wait_call_count = 0;
  auto e1 = [&]() {
    return Eventual<std::string>()
               .start([](auto& k) {
                 k.Start("t1");
               })
        >> Acquire(&lock)
        >> Wait(&lock, [&](Callback<void()> notify) {
             callback = std::move(notify);
             // This predicate returns true on the first call (signaling
             // the need to wait) and false on the second call (signaling
             // that waiting is no longer needed).
             //
             // TODO(xander, benh): Why isn't `value` arg used in this
             // lambda? It's an r-value reference: if it's not used by the
             // predicate, how does it reach the end of the eventual chain?
             // Shouldn't this instead be an l-value reference? If so, why
             // does this compile?
             return [&wait_call_count](std::string&& value) {
               ++wait_call_count;
               // Only keep waiting on the first call.
               return wait_call_count == 1;
             };
           })
        >> Release(&lock);
  };

  auto [future1, t1] = PromisifyForTest(e1());

  Interrupt interrupt;

  // TODO(benh, xander): document why this is needed. Does anything assert that
  // an Interrupt is registered? (For that matter, what's an Interrupt?)
  t1.Register(interrupt);

  t1.Start();

  ASSERT_TRUE(callback);
  // The Wait() predicate is checked once when it's initially reached: our
  // predicate returns true on the first call, so it will trigger a wait. The
  // predicate will return false to unblock things when called again, but it
  // won't be called again until we invoke the callback below.
  //
  // TODO(benh, xander): are predicates only called explicitly, i.e. when
  // someone manually notifies by calling a callback? Document when predicates
  // are called.
  ASSERT_EQ(wait_call_count, 1);

  Lock::Waiter waiter;
  waiter.context = Scheduler::Context::Get();

  ASSERT_TRUE(lock.AcquireFast(&waiter));

  // Notify the waiter that it should try running the predicate again.
  //
  // TODO(benh, xander): document exactly when the predicate is called. Is it
  // during this callback, or in the Release() call below, or at an undefined
  // point after the Release() call but before the future resolves?
  callback();

  lock.Release();

  ASSERT_EQ(wait_call_count, 2);
  EXPECT_EQ("t1", future1.get());
}

// TODO(benh, xander): add tests that call Synchronizable::Wait() where the
// predicate returns true to trigger waiting.
TEST(LockTest, SynchronizableWait) {
  struct Foo : public Synchronizable {
    Foo() {}

    Foo(Foo&& that)
      : Synchronizable() {}

    auto Operation() {
      return Synchronized(
          Just("operation")
          // TODO(benh, xander): document why this lambda needs to take a
          // notify callback arg, and what it should (or shouldn't) do with it.
          // If users commonly won't need to do anything with the callback,
          // should we add an ~overload that accepts an arg-free lambda?
          >> Wait([](Callback<void()> notify) {
              return [](auto&&...) {
                return false;
              };
            }));
    }
  };

  Foo foo;

  // TODO(benh): document why we std::move here, rather than just having 1
  // variable.
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
          >> Then([](int i) {
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
          >> Then([this](int i) {
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
          >> Synchronized(Map([](int i) {
               return ++i;
             }))
          >> Reduce(
                 /* sum = */ 0,
                 [](int& sum) {
                   return Then([&](int i) {
                     sum += i;
                     return true;
                   });
                 });
    }
  };

  Foo foo;

  EXPECT_EQ(5, *foo.Operation());
}


// TODO(benh, xander): add tests that pass predicates to
// ConditionVariable::Wait().
TEST(LockTest, ConditionVariable) {
  struct Foo : public Synchronizable {
    auto WaitFor(int id) {
      return Synchronized(Then([this, id]() {
        auto [iterator, inserted] = condition_variables_.emplace(id, &lock());
        ConditionVariable& condition_variable = iterator->second;
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
              ConditionVariable& condition_variable = iterator->second;
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
              ConditionVariable& condition_variable = iterator->second;
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
