#include "eventuals/static-thread-pool.h"

#include "eventuals/closure.h"
#include "eventuals/eventual.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "gtest/gtest.h"

using eventuals::Closure;
using eventuals::Eventual;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Pinned;
using eventuals::Repeat;
using eventuals::StaticThreadPool;
using eventuals::Then;
using eventuals::Until;

TEST(StaticThreadPoolTest, Schedulable) {
  struct Foo : public StaticThreadPool::Schedulable {
    Foo()
      : StaticThreadPool::Schedulable(Pinned(
          std::thread::hardware_concurrency() - 1)) {}

    auto Operation() {
      return Schedule(
                 Then([this]() {
                   return i;
                 }))
          | Then([](auto i) {
               return i + 1;
             });
    }

    int i = 41;
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}


TEST(StaticThreadPoolTest, Reschedulable) {
  StaticThreadPool::Requirements requirements("reschedulable");
  auto e = [&]() {
    return StaticThreadPool::Scheduler().Schedule(
        &requirements,
        Closure([id = std::this_thread::get_id()]() mutable {
          EXPECT_NE(id, std::this_thread::get_id());
          id = std::this_thread::get_id();
          return Eventual<void>()
                     .start([&id](auto& k) {
                       EXPECT_EQ(id, std::this_thread::get_id());
                       auto thread = std::thread(
                           [&id, &k]() {
                             EXPECT_NE(id, std::this_thread::get_id());
                             k.Start();
                           });
                       thread.detach();
                     })
              | Eventual<void>()
                    .start([&id](auto& k) {
                      EXPECT_EQ(id, std::this_thread::get_id());
                      k.Start();
                    });
        }));
  };

  *e();
}


TEST(StaticThreadPoolTest, PingPong) {
  struct Streamer : public StaticThreadPool::Schedulable {
    Streamer(Pinned pinned)
      : StaticThreadPool::Schedulable(pinned) {}

    auto Stream() {
      using namespace eventuals;

      return Repeat()
          | Until([this]() {
               return Schedule(Then([this]() {
                 return count > 5;
               }));
             })
          | Schedule(Map([this]() mutable {
               return count++;
             }));
    }

    int count = 0;
  };

  struct Listener : public StaticThreadPool::Schedulable {
    Listener(Pinned pinned)
      : StaticThreadPool::Schedulable(pinned) {}

    auto Listen() {
      using namespace eventuals;

      return Schedule(Map([this](int i) {
               count++;
               return i;
             }))
          | Loop()
          | Then([this](auto&&...) {
               return count;
             });
    }

    size_t count = 0;
  };

  Streamer streamer(Pinned(0));
  Listener listener(Pinned(1));

  EXPECT_EQ(6, *(streamer.Stream() | listener.Listen()));
}


TEST(StaticThreadPoolTest, Spawn) {
  auto e = [&]() {
    return StaticThreadPool::Spawn(
        "spawn",
        Closure([id = std::this_thread::get_id()]() mutable {
          EXPECT_NE(id, std::this_thread::get_id());
          id = std::this_thread::get_id();
          return Eventual<void>()
                     .start([&id](auto& k) {
                       EXPECT_EQ(id, std::this_thread::get_id());
                       auto thread = std::thread(
                           [&id, &k]() {
                             EXPECT_NE(id, std::this_thread::get_id());
                             k.Start();
                           });
                       thread.detach();
                     })
              | Eventual<void>()
                    .start([&id](auto& k) {
                      EXPECT_EQ(id, std::this_thread::get_id());
                      k.Start();
                    });
        }));
  };

  *e();
}
