#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/lock.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Acquire;
using stout::eventuals::Eventual;
using stout::eventuals::Lock;
using stout::eventuals::Release;
using stout::eventuals::succeed;
using stout::eventuals::Synchronizable;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(LockTest, Succeed)
{
  Lock lock;

  auto e1 = [&]() {
    return Eventual<std::string>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, "t1");
            });
        thread.detach();
      })
      | Acquire(&lock)
      | [](auto&& value) { return std::move(value); };
  };

  auto e2 = [&]() {
    return Eventual<std::string>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              succeed(k, "t2");
            });
        thread.detach();
      })
      | Acquire(&lock)
      | [](auto&& value) { return std::move(value); };
  };

  auto e3 = [&]() {
    return Release(&lock)
      | []() { return "t3"; };
  };

  auto t1 = eventuals::task(e1());
  auto t2 = eventuals::task(e2());
  auto t3 = eventuals::task(e3());

  eventuals::start(t1);

  EXPECT_EQ("t1", eventuals::wait(t1));

  eventuals::start(t2);

  eventuals::start(t3);

  EXPECT_STREQ("t3", eventuals::wait(t3));

  EXPECT_EQ("t2", eventuals::wait(t2));
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
      | [](auto&& value) { return std::move(value); };
  };

  auto e2 = [&]() {
    return Acquire(&lock)
      | []() { return "t2"; };
  };

  EXPECT_THROW(eventuals::run(eventuals::task(e1())), FailedException);

  EXPECT_STREQ("t2", eventuals::run(eventuals::task(e2())));
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
           stop(k);
         }))
      | Release(&lock);
  };

  auto e2 = [&]() {
    return Acquire(&lock)
      | []() { return "t2"; };
  };

  auto t1 = eventuals::task(e1());

  eventuals::start(t1);

  eventuals::interrupt(t1);

  EXPECT_THROW(eventuals::wait(t1), StoppedException);

  EXPECT_STREQ("t2", eventuals::run(eventuals::task(e2())));
}


TEST(LockTest, Synchronizable)
{
  struct Foo : public Synchronizable
  {
    Foo() : Synchronizable(&lock) {}

    Foo(Foo&& that) : Synchronizable(&lock) {}

    auto Operation()
    {
      return synchronized(
          Eventual<std::string>()
          .start([](auto& k) {
            auto thread = std::thread(
                [&k]() mutable {
                  succeed(k, "operation");
                });
            thread.detach();
          }));
    }

    Lock lock;
  };

  Foo foo;

  Foo foo2 = std::move(foo);

  EXPECT_EQ("operation", eventuals::run(eventuals::task(foo2.Operation())));
}
