#include "eventuals/flat-map.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "event-loop-test.h"
#include "eventuals/collect.h"
#include "eventuals/event-loop.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/range.h"
#include "eventuals/stream.h"
#include "eventuals/timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(FlatMap, TwoLevelLoop) {
  auto s = []() {
    return Range(2)
        >> FlatMap([](int x) { return Range(2); })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1));
}

TEST(FlatMap, FlatMapMapped) {
  auto s = []() {
    return Range(2)
        >> FlatMap([](int x) { return Range(2); })
        >> Map([](int x) { return x + 1; })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 2, 1, 2));
}

TEST(FlatMap, FlatMapIterate) {
  auto s = []() {
    return Range(2)
        >> FlatMap([](int x) {
             std::vector<int> v = {1, 2, 3};
             return Iterate(std::move(v));
           })
        >> Map([](int x) { return x + 1; })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(2, 3, 4, 2, 3, 4));
}

TEST(FlatMap, TwoIndexesSum) {
  auto s = []() {
    return Range(3)
        >> FlatMap([](int x) {
             return Stream<int>()
                 .next([container = std::vector<int>({1, 2}),
                        i = 0u,
                        x](auto& k) mutable {
                   if (i != container.size()) {
                     k.Emit(container[i++] + x);
                   } else {
                     k.Ended();
                   }
                 })
                 .done([](auto& k) {
                   k.Ended();
                 });
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 2, 2, 3, 3, 4));
}

TEST(FlatMap, TwoIndexesSumMap) {
  auto s = []() {
    return Range(3)
        >> FlatMap([](int x) {
             return Range(1, 3)
                 >> Map([x](int y) { return x + y; });
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 2, 2, 3, 3, 4));
}

TEST(FlatMap, Let) {
  auto s = []() {
    return Iterate({1, 2})
        >> FlatMap(Let([](int& x) {
             return Iterate({1, 2})
                 >> FlatMap(Let([&x](int& y) {
                      return Iterate({x, y});
                    }));
           }))
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 1, 1, 2, 2, 1, 2, 2));
}

TEST(FlatMap, FlatMapIterateString) {
  auto s = []() {
    return Iterate(std::vector<std::string>({"abc", "abc"}))
        >> FlatMap([](std::string x) {
             std::vector<int> v = {1, 2, 3};
             return Iterate(std::move(v));
           })
        >> Map([](int x) { return x + 1; })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(2, 3, 4, 2, 3, 4));
}

TEST(FlatMap, ThreeLevelLoop) {
  auto s = []() {
    return Range(2)
        >> FlatMap([](int x) { return Range(2); })
        >> FlatMap([](int x) { return Range(2); })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1, 0, 1, 0, 1));
}

TEST(FlatMap, ThreeLevelLoopInside) {
  auto s = []() {
    return Range(2)
        >> FlatMap([](int x) {
             return Range(2)
                 >> FlatMap([](int y) {
                      return Range(2);
                    });
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1, 0, 1, 0, 1));
}

TEST(FlatMap, ThreeIndexesSumMap) {
  auto s = []() {
    return Range(3)
        >> FlatMap([](int x) {
             return Range(1, 3)
                 >> Map([x](int y) { return x + y; });
           })
        >> FlatMap([](int sum) {
             return Range(1, 3)
                 >> Map([sum](int z) { return sum + z; });
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(2, 3, 3, 4, 3, 4, 4, 5, 4, 5, 5, 6));
}

// Shows that you can stream complex templated objects.
TEST(FlatMap, VectorVector) {
  auto s = []() {
    return Iterate(std::vector<int>({2, 3, 14}))
        >> FlatMap([](int x) {
             std::vector<std::vector<int>> c;
             c.push_back(std::vector<int>());
             c.push_back(std::vector<int>());
             return Iterate(std::move(c));
           })
        >> FlatMap([](std::vector<int> x) { return Range(2); })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1));
}

class FlatMapTest : public EventLoopTest {};

TEST_F(FlatMapTest, Interrupt) {
  auto e = []() {
    return Iterate(std::vector<int>(1000))
        >> Map([](int x) {
             return Timer(std::chrono::milliseconds(100))
                 >> Just(x);
           })
        >> FlatMap([](int x) { return Iterate({1, 2}); })
        >> Collect<std::vector<int>>()
               .stop([](auto& collection, auto& k) {
                 k.Start(std::move(collection));
               });
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().RunUntil(future);

  auto result = future.get();

  CHECK_EQ(result.size(), 0u);
}

TEST(FlatMap, InterruptReturn) {
  std::atomic<bool> waiting = false;

  auto e = [&]() {
    return Iterate(std::vector<int>(1000))
        >> FlatMap([&](int x) {
             return Stream<int>()
                 .interruptible()
                 .begin([&](auto& k, auto& handler) {
                   CHECK(handler) << "Test expects interrupt to be registered";
                   handler->Install([&k]() {
                     k.Stop();
                   });
                   waiting.store(true);
                 })
                 .next([](auto& k, auto&) {
                   k.Ended();
                 });
           })
        >> Collect<std::vector>();
  };

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  ASSERT_FALSE(waiting.load());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  k.Start();

  ASSERT_TRUE(waiting.load());

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

} // namespace
} // namespace eventuals::test
