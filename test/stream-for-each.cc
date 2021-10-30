#include "eventuals/stream-for-each.h"

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
#include "eventuals/terminal.h"
#include "eventuals/timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Collect;
using eventuals::EventLoop;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Just;
using eventuals::Let;
using eventuals::Map;
using eventuals::Range;
using eventuals::Stream;
using eventuals::StreamForEach;
using eventuals::Timer;

using testing::ElementsAre;

TEST(StreamForEach, TwoLevelLoop) {
  auto s = []() {
    return Range(2)
        | StreamForEach([](int x) { return Range(2); })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1));
}

TEST(StreamForEach, StreamForEachMapped) {
  auto s = []() {
    return Range(2)
        | StreamForEach([](int x) { return Range(2); })
        | Map([](int x) { return x + 1; })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 2, 1, 2));
}

TEST(StreamForEach, StreamForEachIterate) {
  auto s = []() {
    return Range(2)
        | StreamForEach([](int x) {
             std::vector<int> v = {1, 2, 3};
             return Iterate(std::move(v));
           })
        | Map([](int x) { return x + 1; })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(2, 3, 4, 2, 3, 4));
}

TEST(StreamForEach, TwoIndexesSum) {
  auto s = []() {
    return Range(3)
        | StreamForEach([](int x) {
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
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 2, 2, 3, 3, 4));
}

TEST(StreamForEach, TwoIndexesSumMap) {
  auto s = []() {
    return Range(3)
        | StreamForEach([](int x) {
             return Range(1, 3)
                 | Map([x](int y) { return x + y; });
           })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 2, 2, 3, 3, 4));
}

TEST(StreamForEach, Let) {
  auto s = []() {
    return Iterate({1, 2})
        | StreamForEach(Let([](int& x) {
             return Iterate({1, 2})
                 | StreamForEach(Let([&x](int& y) {
                      return Iterate({x, y});
                    }));
           }))
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(1, 1, 1, 2, 2, 1, 2, 2));
}

TEST(StreamForEach, StreamForEachIterateString) {
  auto s = []() {
    return Iterate(std::vector<std::string>({"abc", "abc"}))
        | StreamForEach([](std::string x) {
             std::vector<int> v = {1, 2, 3};
             return Iterate(std::move(v));
           })
        | Map([](int x) { return x + 1; })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(2, 3, 4, 2, 3, 4));
}

TEST(StreamForEach, ThreeLevelLoop) {
  auto s = []() {
    return Range(2)
        | StreamForEach([](int x) { return Range(2); })
        | StreamForEach([](int x) { return Range(2); })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1, 0, 1, 0, 1));
}

TEST(StreamForEach, ThreeLevelLoopInside) {
  auto s = []() {
    return Range(2)
        | StreamForEach([](int x) {
             return Range(2)
                 | StreamForEach([](int y) {
                      return Range(2);
                    });
           })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1, 0, 1, 0, 1));
}

TEST(StreamForEach, ThreeIndexesSumMap) {
  auto s = []() {
    return Range(3)
        | StreamForEach([](int x) {
             return Range(1, 3)
                 | Map([x](int y) { return x + y; });
           })
        | StreamForEach([](int sum) {
             return Range(1, 3)
                 | Map([sum](int z) { return sum + z; });
           })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(2, 3, 3, 4, 3, 4, 4, 5, 4, 5, 5, 6));
}

// Shows that you can stream complex templated objects.
TEST(StreamForEach, VectorVector) {
  auto s = []() {
    return Iterate(std::vector<int>({2, 3, 14}))
        | StreamForEach([](int x) {
             std::vector<std::vector<int>> c;
             c.push_back(std::vector<int>());
             c.push_back(std::vector<int>());
             return Iterate(std::move(c));
           })
        | StreamForEach([](std::vector<int> x) { return Range(2); })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*s(), ElementsAre(0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1));
}

class StreamForEachTest : public EventLoopTest {};

TEST_F(StreamForEachTest, Interrupt) {
  auto e = []() {
    return Iterate(std::vector<int>(1000))
        | Map([](int x) {
             return Timer(std::chrono::milliseconds(100))
                 | Just(x);
           })
        | StreamForEach([](int x) { return Iterate({1, 2}); })
        | Collect<std::vector<int>>()
              .stop([](auto& data, auto& k) {
                k.Start(std::move(data));
              });
  };

  auto [future, k] = Terminate(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EventLoop::Default().Run();

  auto result = future.get();

  CHECK_EQ(result.size(), 0u);
}

TEST(StreamForEach, InterruptReturn) {
  std::atomic<bool> waiting = false;

  auto e = [&]() {
    return Iterate(std::vector<int>(1000))
        | StreamForEach([&](int x) {
             return Stream<int>()
                 .interruptible()
                 .begin([&](auto& k, Interrupt::Handler& handler) {
                   handler.Install([&k]() {
                     k.Stop();
                   });
                   waiting.store(true);
                 })
                 .next([](auto& k) {
                   k.Ended();
                 });
           })
        | Collect<std::vector<int>>();
  };

  auto [future, k] = Terminate(e());

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
