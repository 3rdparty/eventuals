#include "eventuals/compose.h"

#include "eventuals/collect.h"
#include "eventuals/concurrent-ordered.h"
#include "eventuals/concurrent.h"
#include "eventuals/conditional.h"
#include "eventuals/do-all.h"
#include "eventuals/event-loop.h"
#include "eventuals/eventual.h"
#include "eventuals/expected.h"
#include "eventuals/filter.h"
#include "eventuals/finally.h"
#include "eventuals/flat-map.h"
#include "eventuals/generator.h"
#include "eventuals/head.h"
#include "eventuals/if.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/lock.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/on-begin.h"
#include "eventuals/on-ended.h"
#include "eventuals/raise.h"
#include "eventuals/range.h"
#include "eventuals/reduce.h"
#include "eventuals/static-thread-pool.h"
#include "eventuals/stream.h"
#include "eventuals/take.h"
#include "eventuals/until.h"
#include "gtest/gtest.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals::test {
namespace {

////////////////////////////////////////////////////////////////////////

TEST(CanCompose, Valid) {
  Lock lock;
  auto acquire = Acquire(&lock);
  auto until = Until([]() { return false; });
  auto release = Release(&lock);

  static_assert(
      CanCompose<decltype(acquire), decltype(until)>);

  static_assert(
      CanCompose<decltype(until), decltype(release)>);
}

TEST(CanCompose, Invalid) {
  auto loop = Loop();
  auto until = Until([]() { return false; });

  static_assert(
      !CanCompose<decltype(loop), decltype(until)>);
}

TEST(CanCompose, ThenExpectsSingleValue) {
  auto loop = Loop();
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(loop), decltype(then)>);
}

TEST(CanCompose, ThenProducesASingleValue) {
  auto then1 = Then([]() { return false; });
  auto then2 = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(then1), decltype(then2)>);
}

TEST(CanCompose, Collect) {
  auto collect = Collect<std::vector>();
  auto then = Then([]() { return false; });
  auto stream = Stream<int>()
                    .context(0)
                    .next([](auto& value, auto& k) {
                      k.Emit(value);
                    })
                    .done([](auto&, auto& k) {
                      k.Ended();
                    });

  static_assert(
      CanCompose<decltype(stream), decltype(collect)>);

  static_assert(
      !CanCompose<decltype(then), decltype(collect)>);
}

TEST(CanCompose, ConcurrentOrderedAdaptor) {
  auto adaptor = ConcurrentOrderedAdaptor();
  auto then = Then([]() { return false; });
  auto map = Map([]() { return 0; });

  static_assert(
      CanCompose<decltype(adaptor), decltype(map)>);

  static_assert(
      !CanCompose<decltype(adaptor), decltype(then)>);

  static_assert(
      CanCompose<decltype(map), decltype(adaptor)>);
}

TEST(CanCompose, ReorderAdaptor) {
  auto reorder = ReorderAdaptor();
  auto then = Then([]() { return false; });
  auto map = Map([]() { return 0; });

  static_assert(
      CanCompose<decltype(reorder), decltype(map)>);

  static_assert(
      !CanCompose<decltype(reorder), decltype(then)>);

  static_assert(
      CanCompose<decltype(map), decltype(reorder)>);
}

TEST(CanCompose, ConcurrentOrdered) {
  auto concurrent_ordered = ConcurrentOrdered([]() { return 0; });
  auto map = Map([]() { return 0; });

  static_assert(
      CanCompose<decltype(concurrent_ordered), decltype(map)>);

  static_assert(
      CanCompose<decltype(map), decltype(concurrent_ordered)>);
}

TEST(CanCompose, Concurrent) {
  auto concurrent =
      Concurrent([]() { return Map([]() { return false; }); });
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(concurrent), decltype(map)>);

  static_assert(
      CanCompose<decltype(map), decltype(concurrent)>);

  static_assert(
      !CanCompose<decltype(then), decltype(concurrent)>);
}

TEST(CanCompose, Conditional) {
  auto then = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("then");
        });
  };

  auto els3 = []() {
    return Eventual<std::string>()
        .start([](auto& k) {
          k.Start("else");
        });
  };

  auto conditional = Conditional(
      [](auto&& i) { return i > 1; },
      [&](auto&&) { return then(); },
      [&](auto&&) { return els3(); });
  auto map = Map([]() { return 0; });
  auto some_then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(some_then), decltype(conditional)>);

  static_assert(
      !CanCompose<decltype(map), decltype(some_then)>);
}

TEST(CanCompose, DoAll) {
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });
  auto do_all = DoAll(
      Eventual<int>([](auto& k) { k.Start(42); }),
      Eventual<std::string>([](auto& k) { k.Start(std::string("hello")); }),
      Eventual<void>([](auto& k) { k.Start(); }));

  static_assert(
      CanCompose<decltype(do_all), decltype(then)>);

  static_assert(
      !CanCompose<decltype(map), decltype(do_all)>);

  static_assert(
      !CanCompose<decltype(do_all), decltype(map)>);
}

TEST(CanCompose, Eventual) {
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });
  auto eventual = Eventual<int>([](auto& k) { k.Start(42); });

  static_assert(
      CanCompose<decltype(eventual), decltype(eventual)>);

  static_assert(
      !CanCompose<decltype(eventual), decltype(map)>);

  static_assert(
      CanCompose<decltype(then), decltype(eventual)>);
}

TEST(CanCompose, Expected) {
  auto f = []() {
    return expected<int>(40);
  };
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(f()), decltype(then)>);

  static_assert(
      !CanCompose<decltype(f()), decltype(map)>);
}

TEST(CanCompose, Filter) {
  auto filter = Filter([]() { return true; });
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });

  static_assert(
      !CanCompose<decltype(filter), decltype(then)>);

  static_assert(
      CanCompose<decltype(map), decltype(filter)>);
}

TEST(CanCompose, Finally) {
  auto finally = Finally([]() { return true; });
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(finally), decltype(then)>);
}

TEST(CanCompose, FlatMap) {
  auto flatmap = FlatMap([]() { return true; });
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });

  static_assert(
      !CanCompose<decltype(flatmap), decltype(then)>);

  static_assert(
      CanCompose<decltype(map), decltype(flatmap)>);
}

TEST(CanCompose, Generator) {
  auto gen = []() -> Generator::Of<int> {
    return []() {
      return Iterate({1, 2, 3});
    };
  };

  auto collect = Collect<std::vector>();
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(gen()), decltype(collect)>);

  static_assert(
      !CanCompose<decltype(gen()), decltype(then)>);

  static_assert(
      CanCompose<decltype(collect), decltype(then)>);
}

TEST(CanCompose, Head) {
  auto head = Head();
  auto stream = Stream<int>()
                    .context(0)
                    .next([](auto& value, auto& k) {
                      k.Emit(value);
                    })
                    .done([](auto&, auto& k) {
                      k.Ended();
                    });
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(stream), decltype(head)>);

  static_assert(
      CanCompose<decltype(head), decltype(then)>);

  static_assert(
      !CanCompose<decltype(then), decltype(head)>);
}

TEST(CanCompose, If) {
  auto some_if = If(true)
                     .yes([]() { return "yes"; })
                     .no([]() { return "no"; });
  auto map = Map([]() { return 0; });
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(then), decltype(some_if)>);

  static_assert(
      !CanCompose<decltype(map), decltype(some_if)>);
}

TEST(CanCompose, OnBeginOnEnd) {
  auto b = OnBegin([]() {});
  auto e = OnEnded([]() {});
  auto stream = Stream<int>()
                    .context(0)
                    .next([](auto& value, auto& k) {
                      k.Emit(value);
                    })
                    .done([](auto&, auto& k) {
                      k.Ended();
                    });
  auto then = Then([]() { return false; });
  auto collect = Collect<std::vector>();

  static_assert(
      CanCompose<decltype(stream), decltype(b)>);

  static_assert(
      CanCompose<decltype(stream), decltype(e)>);

  static_assert(
      CanCompose<decltype(b), decltype(collect)>);

  static_assert(
      CanCompose<decltype(e), decltype(collect)>);

  static_assert(
      !CanCompose<decltype(b), decltype(then)>);

  static_assert(
      !CanCompose<decltype(e), decltype(then)>);
}

TEST(CanCompose, Raise) {
  auto raise = Raise(std::runtime_error("message"));
  auto then = Then([]() { return false; });
  auto map = Map([]() { return 0; });

  static_assert(
      CanCompose<decltype(raise), decltype(then)>);

  static_assert(
      !CanCompose<decltype(raise), decltype(map)>);

  static_assert(
      !CanCompose<decltype(map), decltype(raise)>);
}

TEST(CanCompose, Range) {
  auto collect = Collect<std::vector>();
  auto range = Range(-2, 2);
  auto then = Then([]() { return false; });

  static_assert(
      CanCompose<decltype(range), decltype(collect)>);

  static_assert(
      !CanCompose<decltype(range), decltype(then)>);
}

TEST(CanCompose, Take) {
  std::vector<int> v = {5, 12, 17, 3};

  auto take1 = TakeLast(2);
  auto take2 = TakeRange(1, 2);
  auto then = Then([]() { return false; });
  auto collect = Collect<std::vector>();
  auto iter = Iterate(v);

  static_assert(
      CanCompose<decltype(iter), decltype(take1)>);

  static_assert(
      CanCompose<decltype(iter), decltype(take2)>);

  static_assert(
      CanCompose<decltype(take1), decltype(collect)>);

  static_assert(
      CanCompose<decltype(take2), decltype(collect)>);

  static_assert(
      !CanCompose<decltype(take1), decltype(then)>);

  static_assert(
      !CanCompose<decltype(take2), decltype(then)>);
}

TEST(CanCompose, Schedule) {
  class Actor : public StaticThreadPool::Schedulable {
   public:
    Actor()
      : StaticThreadPool::Schedulable(Pinned::Any()) {}

    auto Function() {
      return Repeat()
          >> Schedule(Map([]() {}));
    }
  };

  Actor actor;

  auto then = Then([]() {});

  static_assert(
      !CanCompose<decltype(actor.Function()), decltype(then)>);
}

TEST(CanCompose, Synchronized) {
  class Object : public Synchronizable {
   public:
    auto Function() {
      return Repeat()
          >> Synchronized(Map([]() {}));
    }
  };

  Object object;

  auto then = Then([]() {});

  static_assert(
      !CanCompose<decltype(object.Function()), decltype(then)>);
}

////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace eventuals::test

////////////////////////////////////////////////////////////////////////
