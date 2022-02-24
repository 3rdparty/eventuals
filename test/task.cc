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
#include "test/expect-throw-what.h"

using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Just;
using eventuals::Map;
using eventuals::Task;
using eventuals::Then;

using testing::MockFunction;

TEST(Task, Succeed) {
  auto e1 = []() -> Task::Of<int> {
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
    return Task::Of<int>::With<int, std::string>(
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
  auto e = [&x]() -> Task::Of<void> {
    return [&x]() {
      return Then([&x]() {
        x = 100;
      });
    };
  };

  *e();

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

  auto e = [&]() -> Task::Of<int> {
    return [&]() {
      return Eventual<int>()
                 .start([](auto& k) {
                   k.Fail(std::runtime_error("error from start"));
                 })
                 .fail([&](auto& k, auto&& error) {
                   functions.fail.Call();
                   k.Fail(std::runtime_error("error from fail"));
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

  EXPECT_THROW_WHAT(*e(), "error from start");
}

TEST(Task, FailTerminated) {
  MockFunction<void()> fail;

  EXPECT_CALL(fail, Call())
      .Times(1);

  auto e = [&]() -> Task::Of<int> {
    return [&]() {
      return Eventual<int>()
                 .start([](auto& k) {
                   k.Fail(std::runtime_error("error from start"));
                 })
                 .fail([&](auto& k, auto&& error) {
                   fail.Call();
                   k.Fail(std::runtime_error("error from fail"));
                 })
          | Then([](int x) { return x + 1; });
    };
  };

  auto [future, k] = Terminate(e());
  k.Fail(std::runtime_error("error"));

  EXPECT_THROW_WHAT(future.get(), "error from fail");
}

TEST(Task, StopOnCallback) {
  MockFunction<void()> stop;

  EXPECT_CALL(stop, Call())
      .Times(0);

  auto e = [&]() -> Task::Of<int> {
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

  auto e = [&]() -> Task::Of<int> {
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
  auto e = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task::Of<int>> task;

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
  auto e = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task::Of<int>> task;

  task.emplace(e());

  Interrupt interrupt;

  std::exception_ptr result;

  task->Fail(
      std::runtime_error("error"),
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

  EXPECT_THROW_WHAT(std::rethrow_exception(result), "error");
}

TEST(Task, StopContinuation) {
  auto e = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task::Of<int>> task;

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

TEST(Task, ConstRef) {
  auto e = []() -> Task::Of<const int&> {
    return []() {
      return Just(42);
    };
  };

  EXPECT_EQ(42, *e());
}

TEST(Task, FromTo) {
  auto task = []() {
    return Task::From<int>::To<std::string>::With<int>(
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

TEST(Task, FromToFail) {
  auto task = []() -> Task::From<int>::To<std::string> {
    return []() {
      return Then([](int&& x) {
        return std::to_string(x);
      });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Fail(std::runtime_error("error"));
               })
        | Just(10)
        | task()
        | Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  EXPECT_THROW_WHAT(*e(), "error");
}

TEST(Task, FromToStop) {
  auto task = []() -> Task::From<int>::To<std::string> {
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

TEST(Task, Success) {
  auto f = []() -> Task::Of<void> {
    return Task::Success();
  };

  auto g = []() -> Task::Of<std::string> {
    return Task::Success(std::string("hello"));
  };

  auto e = [&]() {
    return f()
        | g();
  };

  EXPECT_EQ("hello", *e());
}

TEST(Task, Failure) {
  auto e = []() -> Task::Of<std::string> {
    return Task::Failure("error");
  };

  EXPECT_THROW_WHAT(*e(), "error");
}

TEST(Task, Inheritance) {
  struct Base {
    virtual Task::Of<int> GetTask() = 0;
  };

  struct Sync : public Base {
    Task::Of<int> GetTask() override {
      return Task::Success(10);
    }
  };

  struct Async : public Base {
    Task::Of<int> GetTask() override {
      return []() {
        return Just(20);
      };
    }
  };

  struct Failure : public Base {
    Task::Of<int> GetTask() override {
      return Task::Failure("error");
    }
  };

  auto f = []() -> Task::Of<void> {
    return []() {
      return Just();
    };
  };

  auto sync = [&]() {
    return f()
        | Sync().GetTask();
  };

  auto async = [&]() {
    return f()
        | Async().GetTask();
  };

  auto failure = [&]() {
    return f()
        | Failure().GetTask();
  };

  EXPECT_EQ(*sync(), 10);
  EXPECT_EQ(*async(), 20);
  EXPECT_THROW_WHAT(*failure(), "error");
}

TEST(Task, MoveableSuccess) {
  auto e = []() -> Task::Of<std::unique_ptr<int>> {
    return Task::Success(std::make_unique<int>(10));
  };

  EXPECT_EQ(*(*e()), 10);
}

TEST(Task, ConstRefSuccess) {
  int x = 10;
  auto e = [&]() -> Task::Of<const int&> {
    return Task::Success(std::cref(x));
  };

  auto e1 = [&]() {
    return e()
        | Then([](const int& value) {
             return value + 10;
           });
  };

  auto [future, k] = Terminate(e());

  k.Start();

  x = 42;

  EXPECT_EQ(42, future.get());
  EXPECT_EQ(52, *e1());
}

TEST(Task, ConstRefFunction) {
  int x = 10;
  auto e = [&]() -> Task::Of<const int&> {
    return [&]() {
      return Just(std::cref(x));
    };
  };

  auto [future, k] = Terminate(e());

  k.Start();

  x = 42;

  EXPECT_EQ(42, future.get());
}

TEST(Task, RefFunction) {
  int x = 10;
  auto e = [&]() -> Task::Of<int&> {
    return [&]() {
      return Just(std::ref(x));
    };
  };

  auto e1 = [&]() {
    return e()
        | Then([](auto& v) {
             v += 100;
           });
  };

  *e1();

  EXPECT_EQ(110, x);
}

TEST(Task, RefSuccess) {
  int x = 10;
  auto e = [&]() -> Task::Of<int&> {
    return Task::Success(std::ref(x));
  };

  auto e1 = [&]() {
    return e()
        | Then([](auto& v) {
             v += 100;
           });
  };

  *e1();

  EXPECT_EQ(110, x);
}
