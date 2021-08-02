#include <set>

#include "gtest/gtest.h"

#include "stout/lambda.h"
#include "stout/loop.h"
#include "stout/reduce.h"
#include "stout/repeat.h"
#include "stout/static-thread-pool.h"
#include "stout/terminal.h"
#include "stout/until.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Lambda;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Pinned;
using stout::eventuals::Reduce;
using stout::eventuals::Repeat;
using stout::eventuals::StaticThreadPool;
using stout::eventuals::Stream;

TEST(StaticThreadPoolTest, Schedulable)
{
  struct Foo : public StaticThreadPool::Schedulable
  {
    Foo() : StaticThreadPool::Schedulable(Pinned(3)) {}

    auto Operation()
    {
      return Schedule(
          Lambda([this]() {
            return i;
          }))
        | Lambda([](auto i) {
          return i + 1;
        });
    }

    int i = 41;
  };

  Foo foo;

  EXPECT_EQ(42, *foo.Operation());
}


TEST(StaticThreadPoolTest, PingPong)
{
  struct Streamer : public StaticThreadPool::Schedulable
  {
    Streamer(Pinned pinned) : StaticThreadPool::Schedulable(pinned) {}

    auto Stream()
    {
      using namespace eventuals;

      return Repeat()
        | Until(Schedule(Lambda([this]() {
          return count > 5;
        })))
        | Map(Schedule(Lambda([this]() mutable {
          return count++;
        })));
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
        | Lambda([this](auto&&...) {
          return count;
        });
    }

    size_t count = 0;
  };

  Streamer streamer(Pinned(0));
  Listener listener(Pinned(1));

  EXPECT_EQ(6, *(streamer.Stream() | listener.Listen()));
}


TEST(StaticThreadPoolTest, Parallel)
{
  auto s = []() {
    return Stream<int>()
      .context(5)
      .next([](auto& count, auto& k) {
        if (count > 0) {
          eventuals::emit(k, count--);
        } else {
          eventuals::ended(k);
        }
      })
      .done([](auto&, auto& k) {
        eventuals::ended(k);
      })
      | StaticThreadPool::Scheduler().Parallel([]() {
        return Map(Lambda([](int i) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
          return i + 1;
        }));
      })
      | Reduce(
          std::set<int> { 2, 3, 4, 5, 6 },
          [](auto& values) {
            return Lambda([&values](auto&& value) {
              values.erase(value);
              return true;
            });
          });
  };

  auto values = *s();

  EXPECT_TRUE(values.empty());
}
