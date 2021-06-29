#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/scheduler.h"
#include "stout/task.h"

#include "stout/repeat.h"
#include "stout/then.h"
#include "stout/loop.h"
#include "stout/until.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Pinned;
using stout::eventuals::StaticThreadPool;

TEST(SchedulerTest, Schedulable)
{
  struct Foo : public StaticThreadPool::Schedulable
  {
    Foo() : StaticThreadPool::Schedulable(Pinned(3)) {}

    auto Operation()
    {
      return Schedule(
          [this]() {
            return i;
          })
        | [](auto i) {
          return i + 1;
        };
    }

    int i = 41;
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}


TEST(SchedulerTest, PingPong)
{
  struct Streamer : public StaticThreadPool::Schedulable
  {
    Streamer(Pinned pinned) : StaticThreadPool::Schedulable(pinned) {}

    auto Stream()
    {
      using namespace eventuals;

      return Repeat()
        | Until(Schedule([this]() {
          return count > 5;
        }))
        | Map(Schedule([this]() mutable {
          return count++;
        }));
    }

    int count = 0;
  };

  struct Listener : public StaticThreadPool::Schedulable
  {
    Listener(Pinned pinned) : StaticThreadPool::Schedulable(pinned) {}

    auto Listen()
    {
      using namespace eventuals;

      return Map(
          Schedule(Lambda([this](int i) {
            count++;
            return i;
          })))
        | Loop()
        | [this](auto&&...) {
          return count;
        };
    }

    size_t count = 0;
  };

  Streamer streamer(Pinned(0));
  Listener listener(Pinned(1));

  EXPECT_EQ(6, *(streamer.Stream() | listener.Listen()));
}
