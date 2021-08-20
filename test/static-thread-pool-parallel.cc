#include <set>

#include "gtest/gtest.h"
#include "stout/eventual.h"
#include "stout/lambda.h"
#include "stout/raise.h"
#include "stout/reduce.h"
#include "stout/static-thread-pool.h"
#include "stout/terminal.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Eventual;
using stout::eventuals::Lambda;
using stout::eventuals::Raise;
using stout::eventuals::Reduce;
using stout::eventuals::StaticThreadPool;
using stout::eventuals::Stream;

TEST(StaticThreadPoolTest, Parallel) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | StaticThreadPool::Scheduler().Parallel([]() {
            return Lambda([](int i) {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              return i + 1;
            });
          })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
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


TEST(StaticThreadPoolTest, ParallelDone) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 if (count > 0) {
                   k.Emit(count--);
                 } else {
                   k.Ended();
                 }
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | StaticThreadPool::Scheduler().Parallel([]() {
            return Lambda([](int i) {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              return i + 1;
            });
          })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Lambda([&values](auto&& value) {
                   values.erase(value);
                   return false;
                 });
               });
  };

  auto values = *s();

  EXPECT_EQ(4, values.size());
}


TEST(StaticThreadPoolTest, ParallelIngressFail) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Fail("error");
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | StaticThreadPool::Scheduler().Parallel([]() {
            return Lambda([](int i) {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              return i + 1;
            });
          })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Lambda([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), std::exception_ptr);
}


TEST(StaticThreadPoolTest, ParallelIngressStop) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Stop();
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | StaticThreadPool::Scheduler().Parallel([]() {
            return Lambda([](int i) {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              return i + 1;
            });
          })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Lambda([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), eventuals::StoppedException);
}


TEST(StaticThreadPoolTest, ParallelWorkerFail) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Emit(count--);
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | StaticThreadPool::Scheduler().Parallel([]() {
            return Raise("error");
          })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Lambda([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), std::exception_ptr);
}


TEST(StaticThreadPoolTest, ParallelWorkerStop) {
  auto s = []() {
    return Stream<int>()
               .context(5)
               .next([](auto& count, auto& k) {
                 k.Emit(count--);
               })
               .done([](auto&, auto& k) {
                 k.Ended();
               })
        | StaticThreadPool::Scheduler().Parallel([]() {
            return Eventual<int>()
                .start([](auto& k, auto&&...) {
                  k.Stop();
                });
          })
        | Reduce(
               std::set<int>{2, 3, 4, 5, 6},
               [](auto& values) {
                 return Lambda([&values](auto&& value) {
                   values.erase(value);
                   return true;
                 });
               });
  };

  EXPECT_THROW(*s(), eventuals::StoppedException);
}
