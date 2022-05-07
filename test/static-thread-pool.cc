#include "eventuals/static-thread-pool.h"

#include <vector>

#include "eventuals/closure.h"
#include "eventuals/collect.h"
#include "eventuals/concurrent.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/repeat.h"
#include "eventuals/scheduler.h"
#include "eventuals/then.h"
#include "eventuals/until.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::UnorderedElementsAre;

TEST(StaticThreadPoolTest, Schedulable) {
  struct Foo : public StaticThreadPool::Schedulable {
    Foo()
      : StaticThreadPool::Schedulable(Pinned::ExactCPU(
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

  Streamer streamer(Pinned::ExactCPU(0));
  Listener listener(Pinned::ExactCPU(1));

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

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  *e();
}

TEST(StaticThreadPoolTest, SpawnFail) {
  auto e = [&]() {
    return StaticThreadPool::Spawn(
        "spawn",
        Closure([id = std::this_thread::get_id()]() mutable {
          EXPECT_NE(id, std::this_thread::get_id());
          id = std::this_thread::get_id();
          return Eventual<void>()
                     .raises<std::runtime_error>()
                     .start([&id](auto& k) {
                       EXPECT_EQ(id, std::this_thread::get_id());
                       auto thread = std::thread(
                           [&id, &k]() {
                             EXPECT_NE(id, std::this_thread::get_id());
                             k.Fail(std::runtime_error("error"));
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

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THROW(*e(), std::runtime_error);
}

TEST(StaticThreadPoolTest, Concurrent) {
  StaticThreadPool::Requirements requirements(
      "modulo total CPUs 2",
      Pinned::ModuloTotalCPUs(2));

  auto e = [&]() {
    return StaticThreadPool::Scheduler().Schedule(
        "static thread pool",
        &requirements,
        Iterate({1, 2, 3})
            | Concurrent([&requirements]() {
                auto parent = Scheduler::Context::Get().reborrow();
                EXPECT_EQ(
                    parent->name(),
                    "static thread pool");
                EXPECT_EQ(&requirements, parent->data);
                return Map([&requirements, parent = std::move(parent)](int i) {
                  // Should be executed on a cloned StaticThreadPool context.
                  auto child = Scheduler::Context::Get().reborrow();
                  EXPECT_NE(parent.get(), child.get());
                  EXPECT_EQ(&requirements, child->data);
                  EXPECT_EQ(
                      child->name(),
                      "static thread pool [concurrent fiber]");
                  return i;
                });
              })
            | Collect<std::vector<int>>());
  };

  EXPECT_THAT(*e(), UnorderedElementsAre(1, 2, 3));
}

} // namespace
} // namespace eventuals::test
