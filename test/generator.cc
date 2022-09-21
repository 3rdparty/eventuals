#include "eventuals/generator.h"

#include <atomic>
#include <thread>
#include <vector>

#include "eventuals/closure.h"
#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/flat-map.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/just.h"
#include "eventuals/lazy.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/range.h"
#include "eventuals/stream.h"
#include "eventuals/task.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(Generator, Succeed) {
  auto stream = []() -> Generator::Of<int> {
    return []() {
      return Iterate({1, 2, 3});
    };
  };

  auto e1 = [&]() {
    return stream()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e1(), ElementsAre(1, 2, 3));

  auto e2 = [&]() {
    return stream()
        >> Loop<int>()
               .body([](auto& k, auto&&) {
                 k.Done();
               })
               .ended([](auto& k) {
                 k.Start(0);
               });
  };

  EXPECT_EQ(*e2(), 0);

  auto e3 = [&stream]() {
    return stream()
        >> Map([](auto x) {
             return x + 1;
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e3(), ElementsAre(2, 3, 4));

  auto stream2 = []() {
    return Generator::Of<int>::With<std::vector<int>>(
        {1, 2, 3},
        [](auto v) {
          return Iterate(std::move(v));
        });
  };

  auto e4 = [&stream2]() {
    return stream2()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e4(), ElementsAre(1, 2, 3));
}

TEST(Generator, GeneratorWithNonCopyable) {
  struct NonCopyable {
    NonCopyable(NonCopyable&&) = default;
    NonCopyable(const NonCopyable&) = delete;

    int x;
  };

  auto generator = []() {
    return Generator::Of<int>::With<NonCopyable>(
        NonCopyable{100},
        [](NonCopyable& non_copyable) {
          return Iterate({non_copyable.x});
        });
  };

  auto e = [&]() {
    return generator()
        >> Loop<int>()
               .context(0)
               .body([](auto& sum, auto& stream, auto&& value) {
                 sum += value;
                 stream.Next();
               })
               .ended([](auto& sum, auto& k) {
                 k.Start(sum);
               });
  };

  EXPECT_EQ(*e(), 100);
}

TEST(Generator, GeneratorWithPtr) {
  auto generator = []() {
    return Generator::Of<int>::With<int*>(
        new int(100),
        [](auto ptr) {
          static_assert(std::is_same_v<decltype(ptr), int*>);

          int value = *ptr;
          delete ptr;

          return Iterate({value});
        });
  };

  auto e = [&]() {
    return generator()
        >> Loop<int>()
               .context(0)
               .body([](auto& sum, auto& stream, auto&& value) {
                 sum += value;
                 stream.Next();
               })
               .ended([](auto& sum, auto& k) {
                 k.Start(sum);
               });
  };

  EXPECT_EQ(*e(), 100);
}

TEST(Generator, InterruptStream) {
  struct Functions {
    MockFunction<void()> next, done, ended, fail, stop;
  };

  Functions functions;

  EXPECT_CALL(functions.next, Call())
      .Times(1);

  EXPECT_CALL(functions.done, Call())
      .Times(0);

  EXPECT_CALL(functions.ended, Call())
      .Times(0);

  EXPECT_CALL(functions.fail, Call())
      .Times(0);

  EXPECT_CALL(functions.stop, Call())
      .Times(1);

  auto stream = [&functions]() -> Generator::Of<int> {
    return [&]() {
      return Stream<int>()
          .context(Lazy<std::atomic<bool>>(false))
          .interruptible()
          .begin([](auto&, auto& k, auto& handler) {
            CHECK(handler) << "Test expects interrupt to be registered";
            handler->Install([&k]() {
              k.Stop();
            });
            k.Begin();
          })
          .next([&](auto& interrupted, auto& k, auto&) {
            functions.next.Call();
            k.Emit(1);
          })
          .done([&](auto&, auto& k, auto&) {
            functions.done.Call();
          });
    };
  };

  Interrupt interrupt;

  auto e = [&]() {
    return stream()
        >> Loop<int>()
               .body([&](auto& k, auto&&) {
                 interrupt.Trigger();
               })
               .ended([&](auto&) {
                 functions.ended.Call();
               })
               .fail([&](auto&, auto&&) {
                 functions.fail.Call();
               })
               .stop([&](auto& k) {
                 functions.stop.Call();
                 k.Stop();
               });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST(Generator, FailStream) {
  struct Functions {
    MockFunction<void()> next, done, ended, fail, stop, body;
  };

  Functions functions;

  EXPECT_CALL(functions.next, Call())
      .Times(0);

  EXPECT_CALL(functions.done, Call())
      .Times(0);

  EXPECT_CALL(functions.ended, Call())
      .Times(0);

  EXPECT_CALL(functions.fail, Call())
      .Times(2);

  EXPECT_CALL(functions.stop, Call())
      .Times(0);

  EXPECT_CALL(functions.body, Call())
      .Times(0);

  auto stream = [&functions]()
      -> Generator::Of<int>::Raises<std::runtime_error> {
    return [&]() {
      return Stream<int>()
          .next([&](auto& k) {
            functions.next.Call();
          })
          .done([&](auto& k) {
            functions.done.Call();
          })
          .fail([&](auto& k, auto&& error) {
            // No need to specify 'raises' because type of error is
            // 'std::exception_ptr', that just propagates.
            functions.fail.Call();
            k.Fail(error);
          })
          .stop([&](auto& k) {
            functions.stop.Call();
          });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .raises<std::runtime_error>()
               .start([](auto& k) {
                 k.Fail(std::runtime_error("error"));
               })
        >> stream()
        >> Loop<int>()
               .body([&](auto& k, auto&&) {
                 functions.body.Call();
               })
               .ended([&](auto&) {
                 functions.ended.Call();
               })
               .fail([&](auto& k, auto&& error) {
                 // No need to specify 'raises' because type of error is
                 // 'std::exception_ptr', that just propagates.
                 functions.fail.Call();
                 k.Fail(std::forward<decltype(error)>(error));
               })
               .stop([&](auto& k) {
                 functions.stop.Call();
               });
  };

  auto [future, k] = PromisifyForTest(e());

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  k.Start();

  EXPECT_THAT(
      // NOTE: capturing 'future' as a pointer because until C++20 we
      // can't capture a "local binding" by reference and there is a
      // bug with 'EXPECT_THAT' that forces our lambda to be const so
      // if we capture it by copy we can't call 'get()' because that
      // is a non-const function.
      [future = &future]() { future->get(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}

TEST(Generator, StopStream) {
  struct Functions {
    MockFunction<void()> next, done, ended, fail, stop, body;
  };

  Functions functions;

  EXPECT_CALL(functions.next, Call())
      .Times(0);

  EXPECT_CALL(functions.done, Call())
      .Times(0);

  EXPECT_CALL(functions.ended, Call())
      .Times(0);

  EXPECT_CALL(functions.fail, Call())
      .Times(0);

  EXPECT_CALL(functions.stop, Call())
      .Times(2);

  EXPECT_CALL(functions.body, Call())
      .Times(0);

  auto stream = [&functions]() -> Generator::Of<int> {
    return [&]() {
      return Stream<int>()
          .next([&](auto& k) {
            functions.next.Call();
          })
          .done([&](auto& k) {
            functions.done.Call();
          })
          .fail([&](auto& k, auto&& error) {
            functions.fail.Call();
          })
          .stop([&](auto& k) {
            functions.stop.Call();
            k.Stop();
          });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Stop();
               })
        >> stream()
        >> Loop<int>()
               .body([&](auto& k, auto&&) {
                 functions.body.Call();
               })
               .ended([&](auto&) {
                 functions.ended.Call();
               })
               .fail([&](auto& k, auto&& error) {
                 functions.fail.Call();
               })
               .stop([&](auto& k) {
                 functions.stop.Call();
                 k.Stop();
               });
  };

  auto [future, k] = PromisifyForTest(e());

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST(Generator, TaskWithGenerator) {
  auto stream = []() -> Generator::Of<int> {
    return []() {
      return Iterate({1, 2, 3});
    };
  };

  auto task = [&]() -> Task::Of<std::vector<int>> {
    return [&]() {
      return stream()
          >> Collect<std::vector>();
    };
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(task())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_THAT(*task(), ElementsAre(1, 2, 3));
}

TEST(Generator, Void) {
  struct Functions {
    MockFunction<void()> next, done, ended, body;
  };

  Functions functions;

  EXPECT_CALL(functions.next, Call())
      .Times(1);

  EXPECT_CALL(functions.done, Call())
      .Times(1);

  EXPECT_CALL(functions.ended, Call())
      .Times(1);

  EXPECT_CALL(functions.body, Call())
      .Times(1);

  auto stream = [&]() -> Generator::Of<void> {
    return [&]() {
      return Stream<void>()
          .next([&](auto& k) {
            functions.next.Call();
            k.Emit();
          })
          .done([&](auto& k) {
            functions.done.Call();
            k.Ended();
          });
    };
  };

  auto e = [&]() {
    return stream()
        >> Loop<void>()
               .body([&](auto& stream) {
                 functions.body.Call();
                 stream.Done();
               })
               .ended([&](auto& k) {
                 functions.ended.Call();
                 k.Start();
               });
  };

  *e();
}

TEST(Generator, FlatMap) {
  auto stream = []() -> Generator::Of<int> {
    return []() {
      return Iterate({1, 2, 3})
          >> FlatMap([](int i) {
               return Range(0, i);
             });
    };
  };

  auto e = [&]() {
    return stream()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(0, 0, 1, 0, 1, 2));
}

TEST(Generator, ConstRef) {
  std::vector<int> v = {1, 2, 3};
  auto stream = [&]() -> Generator::Of<const int&> {
    return [&]() {
      return Iterate(v);
    };
  };

  auto e = [&]() {
    return stream()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3));
}

TEST(Generator, FromTo) {
  auto stream = [data = std::vector<int>()]() mutable {
    return Generator::From<std::string>::To<int>(
        [&]() {
          return Closure([&]() {
                   return Then([&](auto value) mutable {
                     for (auto c : value) {
                       data.push_back(c - '0');
                     }
                   });
                 })
              >> Closure([&]() {
                   return Iterate(std::move(data));
                 });
        });
  };

  auto e = [&]() {
    return Just(std::string("123"))
        >> stream()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3));
}

TEST(Generator, FromToLValue) {
  auto stream = [data = std::vector<int>()]() mutable {
    return Generator::From<std::string>::To<int>(
        [&]() {
          return Closure([&]() mutable {
                   return Then([&](auto value) mutable {
                     for (auto c : value) {
                       data.push_back(c - '0');
                     }
                   });
                 })
              >> Iterate(data);
        });
  };

  auto e = [&]() {
    return Just(std::string("123"))
        >> stream()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3));
}

TEST(Generator, Raises) {
  auto stream = [&]() -> Generator::Of<int>::Raises<std::runtime_error> {
    return [&]() {
      return Stream<int>()
          .raises<std::runtime_error>()
          .next([&](auto& k) {
            k.Fail(std::runtime_error("error"));
          });
    };
  };

  auto e = [&]() {
    return stream()
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THROW(*e(), std::runtime_error);
}

TEST(Generator, StaticHeapSize) {
  auto stream = []() -> Generator::Of<int> {
    return []() {
      return Iterate({1, 2, 3});
    };
  };

  auto e = [&]() {
    return stream()
        >> Collect<std::vector>();
  };

  auto [future, k] = PromisifyForTest(e());

  EXPECT_GT(k.StaticHeapSize().bytes(), 0);
}

TEST(Generator, MonotonicBuffer) {
  auto stream = []() -> Generator::Of<int> {
    return []() {
      return Iterate({1, 2, 3});
    };
  };

  auto e = [&]() {
    return stream()
        >> Collect<std::vector>();
  };

  std::optional<stout::Borrowable<
      std::pmr::monotonic_buffer_resource>>
      resource;

  auto [future, k] = PromisifyForTest(e());

  Bytes static_heap_size = k.StaticHeapSize();

  EXPECT_GT(static_heap_size, 0);

  char* buffer = new char[static_heap_size.bytes()];

  resource.emplace(
      buffer,
      static_heap_size.bytes());

  k.Register(resource->Borrow());

  k.Start();

  EXPECT_EQ(future.get(), (std::vector<int>{1, 2, 3}));

  delete[] buffer;
}

} // namespace
} // namespace eventuals::test
