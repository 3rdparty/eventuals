#include "eventuals/task.h"

#include <string>

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

TEST(Task, Succeed) {
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

TEST(Task, FailTerminated) {
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

TEST(Task, StopTerminated) {
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

TEST(Task, Start) {
  auto e = []() -> Task<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task<int>> task;

  task.emplace(e());

  Interrupt interrupt;

  int result = 0;

  task->Start(
      interrupt,
      [&](int x) {
        result = x;
      },
      [](std::exception_ptr) {
        FAIL() << "test should not have failed";
      },
      []() {
        FAIL() << "test should not have stopped";
      });

  EXPECT_EQ(42, result);
}

TEST(Task, FailContinuation) {
  auto e = []() -> Task<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task<int>> task;

  task.emplace(e());

  Interrupt interrupt;

  std::exception_ptr result;

  task->Fail(
      "error",
      interrupt,
      [](int) {
        FAIL() << "test should not have succeeded";
      },
      [&](std::exception_ptr exception) {
        result = std::move(exception);
      },
      []() {
        FAIL() << "test should not have stopped";
      });

  EXPECT_THROW(std::rethrow_exception(result), const char*);
}

TEST(Task, StopContinuation) {
  auto e = []() -> Task<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task<int>> task;

  task.emplace(e());

  Interrupt interrupt;

  bool stopped = false;

  task->Stop(
      interrupt,
      [](int) {
        FAIL() << "test should not have succeeded";
      },
      [](std::exception_ptr) {
        FAIL() << "test should not have failed";
      },
      [&]() {
        stopped = true;
      });

  EXPECT_TRUE(stopped);
}

TEST(TaskTest, ConstRef) {
  auto e = []() -> Task<const int&> {
    return []() {
      return Just(42);
    };
  };

  EXPECT_EQ(42, *e());
}

TEST(Tast, FromTo) {
  auto task = []() {
    return Task<int, std::string>::With<int>(
        10,
        [](auto x) {
          return Then([x](auto&& value) {
            value += x;
            return std::to_string(value);
          });
        });
  };

  auto e = [&]() {
    return Just(10)
        | task()
        | Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  EXPECT_EQ(*e(), "201");
}

TEST(Tast, FromToFail) {
  auto task = []() -> Task<int, std::string> {
    return []() {
      return Then([](int&& x) {
        return std::to_string(x);
      });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Fail("error");
               })
        | Just(10)
        | task()
        | Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  EXPECT_THROW(*e(), std::exception_ptr);
}

TEST(Tast, FromToStop) {
  auto task = []() -> Task<int, std::string> {
    return []() {
      return Then([](int&& x) {
        return std::to_string(x);
      });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Stop();
               })
        | Just(10)
        | task()
        | Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  EXPECT_THROW(*e(), eventuals::StoppedException);
}
