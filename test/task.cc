#include "eventuals/task.h"

#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/just.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Just;
using eventuals::Map;
using eventuals::Task;
using eventuals::Then;

using testing::MockFunction;

TEST(TaskTest, Succeed) {
  auto e1 = []() -> Task<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  EXPECT_EQ(42, *e1());

  auto e2 = [&]() {
    return e1()
        | Then([](int i) {
             return i + 1;
           })
        | e1();
  };

  EXPECT_EQ(42, *e2());

  auto e3 = []() {
    return Task<int>::With<int, std::string>(
        42,
        "hello world",
        [](auto i, auto s) {
          return Just(i);
        });
  };

  EXPECT_EQ(42, *e3());

  auto e4 = [&]() {
    return e3()
        | Then([](int i) {
             return i + 1;
           })
        | e3();
  };

  EXPECT_EQ(42, *e4());
}

TEST(Task, Void) {
  int x = 0;
  auto e1 = [&x]() -> Task<void> {
    return [&x]() {
      return Then([&x]() {
        x = 100;
      });
    };
  };

  *e1();

  EXPECT_EQ(100, x);
}

TEST(Task, FailOnCallback) {
  struct Functions {
    MockFunction<void()> fail, stop, start;
  };

  Functions functions;

  EXPECT_CALL(functions.fail, Call())
      .Times(1);

  EXPECT_CALL(functions.stop, Call())
      .Times(0);

  EXPECT_CALL(functions.start, Call())
      .Times(0);

  auto e = [&]() -> Task<int> {
    return [&]() {
      return Eventual<int>()
                 .start([](auto& k) {
                   k.Fail("error");
                 })
                 .fail([&](auto& k, auto&& error) {
                   functions.fail.Call();
                   k.Fail("error");
                 })
          | Then([](int) { return 1; })
          | Eventual<int>()
                .start([&](auto& k, auto&& value) {
                  functions.start.Call();
                })
                .stop([&](auto&) {
                  functions.stop.Call();
                })
                .fail([&](auto& k, auto&& error) {
                  functions.fail.Call();
                  k.Fail(error);
                });
    };
  };

  EXPECT_THROW(*e(), std::exception_ptr);
}

TEST(Task, Fail) {
  MockFunction<void()> fail;

  EXPECT_CALL(fail, Call())
      .Times(1);

  auto e = [&]() -> Task<int> {
    return [&]() {
      return Eventual<int>()
                 .start([](auto& k) {
                   k.Fail("error");
                 })
                 .fail([&](auto& k, auto&& error) {
                   fail.Call();
                   k.Fail("error");
                 })
          | Then([](int x) { return x + 1; });
    };
  };

  auto [future, k] = Terminate(e());
  k.Fail("error");

  EXPECT_THROW(future.get(), std::exception_ptr);
}

TEST(Task, StopOnCallback) {
  MockFunction<void()> stop;

  EXPECT_CALL(stop, Call())
      .Times(0);

  auto e = [&]() -> Task<int> {
    return [&]() {
      return Eventual<int>()
          .start([](auto& k) {
            k.Stop();
          })
          .stop([&](auto& k) {
            stop.Call();
            k.Stop();
          });
    };
  };

  EXPECT_THROW(*e(), eventuals::StoppedException);
}

TEST(Task, Stop) {
  MockFunction<void()> stop;

  EXPECT_CALL(stop, Call())
      .Times(1);

  auto e = [&]() -> Task<int> {
    return [&]() {
      return Eventual<int>()
          .start([](auto& k) {
            k.Stop();
          })
          .stop([&](auto& k) {
            stop.Call();
            k.Stop();
          });
    };
  };

  auto [future, k] = Terminate(e());
  k.Stop();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST(TaskTest, Start) {
  auto e = []() -> Task<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  Interrupt interrupt;
  e().Start(
      interrupt,
      [](int x) {
        EXPECT_EQ(x, 42);
      },
      [](std::exception_ptr) {
        FAIL() << "Test should not been failed";
      },
      []() {
        FAIL() << "Test should not been stopped";
      });
}