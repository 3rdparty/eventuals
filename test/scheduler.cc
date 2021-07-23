#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/static-thread-pool.h"
#include "stout/task.h"

#include "stout/just.h"
#include "stout/loop.h"
#include "stout/repeat.h"
#include "stout/until.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Just;
using stout::eventuals::Pinned;
using stout::eventuals::StaticThreadPool;

// TEST(SchedulerTest, Schedulable)
// {
//   struct Foo : public StaticThreadPool::Schedulable
//   {
//     Foo() : StaticThreadPool::Schedulable(Pinned(3)) {}

//     auto Operation()
//     {
//       return Schedule(
//           [this]() {
//             return Just(i);
//           })
//         | [](auto i) {
//           return i + 1;
//         };
//     }

//     int i = 41;
//   };

//   Foo foo;

//   EXPECT_EQ(42, *foo.Operation());
// }


// TEST(SchedulerTest, PingPong)
// {
//   struct Streamer : public StaticThreadPool::Schedulable
//   {
//     Streamer(Pinned pinned) : StaticThreadPool::Schedulable(pinned) {}

//     auto Stream()
//     {
//       using namespace eventuals;

//       return Repeat()
//         | Until(Schedule([this]() {
//           return Just(count > 5);
//         }))
//         | Map(Schedule([this]() mutable {
//           return Just(count++);
//         }));
//     }

//     int count = 0;
//   };

//   struct Listener : public StaticThreadPool::Schedulable
//   {
//     Listener(Pinned pinned) : StaticThreadPool::Schedulable(pinned) {}

//     auto Listen()
//     {
//       using namespace eventuals;

//       // return Reduce(
//       //     /* count = */ 0,
//       //     [](auto& count) {
//       //       return Schedule([&](int) {
//       //         std::cout << "listener: " << std::this_thread::get_id() << std::endl;
//       //         ++count;
//       //         return true; // Continue reducing.
//       //       });
//       //     });

//       // return Map(
//       //     Schedule([this](int i) {
//       //       count++;
//       //       return Just(i);
//       //     }));


//       // return Map(Schedule([this]() { return Ingress(); }))

//       // if constexpr (IsContinuation<E>::value) {
//       //   return ScheduleClosure([e = std::move(e)]() {
//       //     return std::move(e);
//       //   });
//       // } else {
//       //   return ScheduleClosure([f = std::move(f)]() {
//       //     return Then(std::move(f));
//       //   });
//       // }


//       // return Map(
//       //     ScheduleThen([this](int i) {
//       //       count++;
//       //       return Just(i);
//       //     }))

//       // return Map(Schedule([this]() {
//       //   return Then([this](int i) {
//       //     count++;
//       //     return Just(i);
//       //   }))))


        


//       return Map(
//           Schedule([this](int i) {
//             count++;
//             return Just(i);
//           }))
//         | Loop()
//         | [this](auto&&...) {
//           return count;
//         };
//     }

//     size_t count = 0;
//   };

//   Streamer streamer(Pinned(0));
//   Listener listener(Pinned(1));

//   EXPECT_EQ(6, *(streamer.Stream() | listener.Listen()));
// }

#include <deque>
#include <vector>
#include <future>

#include "stout/interrupt.h"
#include "stout/lambda.h"
#include "stout/lock.h"
#include "stout/reduce.h"
#include "stout/then.h"

using stout::Callback;
using stout::Undefined;

using stout::eventuals::Acquire;
using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::Lambda;
using stout::eventuals::Lock;
using stout::eventuals::Loop;
using stout::eventuals::Map;
using stout::eventuals::Reduce;
using stout::eventuals::Release;
using stout::eventuals::Repeat;
using stout::eventuals::Task;
using stout::eventuals::Terminal;
using stout::eventuals::Then;
using stout::eventuals::Wait;

using stout::eventuals::Stream;

TEST(SchedulerTest, Parallel)
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
