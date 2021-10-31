#include "eventuals/generator.h"

#include <atomic>
#include <thread>
#include <vector>

#include "eventuals/collect.h"
#include "eventuals/context.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/range.h"
#include "eventuals/stream-for-each.h"
#include "eventuals/stream.h"
#include "eventuals/task.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Collect;
using eventuals::Context;
using eventuals::Eventual;
using eventuals::Generator;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Range;
using eventuals::Stream;
using eventuals::StreamForEach;
using eventuals::Task;
using eventuals::Terminate;

using testing::ElementsAre;
using testing::MockFunction;

// NOTE: We can't use std::initializer_list at
// Generator's lamda, because lambda captures this
// list as class member, so we can't store it at Callback.

TEST(Generator, Succeed) {
  auto stream = []() -> Generator<int> {
    return []() {
      std::vector<int> v = {1, 2, 3};
      return Iterate(std::move(v));
    };
  };

  auto e1 = [&]() {
    return stream()
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e1(), ElementsAre(1, 2, 3));

  auto e2 = [&]() {
    return stream()
        | Loop<int>()
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
        | Map([](auto x) {
             return x + 1;
           })
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e3(), ElementsAre(2, 3, 4));

  auto stream2 = []() {
    return Generator<int>::With<std::vector<int>>(
        {1, 2, 3},
        [](auto v) {
          return Iterate(std::move(v));
        });
  };

  auto e4 = [&stream2]() {
    return stream2()
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e4(), ElementsAre(1, 2, 3));
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

  auto stream = [&functions]() -> Generator<int> {
    return [&]() {
      return Stream<int>()
          .context(Context<std::atomic<bool>>(false))
          .interruptible()
          .begin([](auto&, auto& k, Interrupt::Handler& handler) {
            handler.Install([&k]() {
              k.Stop();
            });
            k.Begin();
          })
          .next([&](auto& interrupted, auto& k) {
            functions.next.Call();
            k.Emit(1);
          })
          .done([&](auto&, auto& k) {
            functions.done.Call();
          });
    };
  };

  Interrupt interrupt;

  auto e = [&]() {
    return stream()
        | Loop<int>()
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

  auto [future, k] = Terminate(e());

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

  auto stream = [&functions]() -> Generator<int> {
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
            k.Fail(error);
          })
          .stop([&](auto& k) {
            functions.stop.Call();
          });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Fail("error");
               })
        | stream()
        | Loop<int>()
              .body([&](auto& k, auto&&) {
                functions.body.Call();
              })
              .ended([&](auto&) {
                functions.ended.Call();
              })
              .fail([&](auto& k, auto&& error) {
                functions.fail.Call();
                k.Fail(error);
              })
              .stop([&](auto& k) {
                functions.stop.Call();
              });
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), std::exception_ptr);
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

  auto stream = [&functions]() -> Generator<int> {
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
        | stream()
        | Loop<int>()
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

  auto [future, k] = Terminate(e());

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST(Generator, TaskWithGenerator) {
  auto stream = []() -> Generator<int> {
    return []() {
      std::vector<int> v = {1, 2, 3};
      return Iterate(std::move(v));
    };
  };

  auto task = [&]() -> Task<std::vector<int>> {
    return [&]() {
      return stream()
          | Collect<std::vector<int>>();
    };
  };

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

  auto stream = [&]() -> Generator<void> {
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
        | Loop<void>()
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

TEST(Generator, StreamForEach) {
  auto stream = []() -> Generator<int> {
    return []() {
      std::vector<int> v = {1, 2, 3};
      return Iterate(std::move(v))
          | StreamForEach([](int i) {
               return Range(0, i);
             });
    };
  };

  auto e = [&]() {
    return stream()
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e(), ElementsAre(0, 0, 1, 0, 1, 2));
}
