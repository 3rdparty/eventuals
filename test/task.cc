#include "eventuals/task.h"

#include <string>

#include "eventuals/catch.h"
#include "eventuals/do-all.h"
#include "eventuals/eventual.h"
#include "eventuals/just.h"
#include "eventuals/map.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/generate-test-task-name.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::ThrowsMessage;

TEST(Task, Succeed) {
  auto e1 = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  EXPECT_EQ(42, *e1());

  auto e2 = [&]() {
    return e1()
        >> Then([](int i) {
             return i + 1;
           })
        >> e1();
  };

  EXPECT_EQ(42, *e2());

  auto e3 = []() {
    return Task::Of<int>::With<int, std::string>(
        42,
        "hello world",
        [](auto i, auto s) {
          EXPECT_EQ(s, "hello world");
          return Just(i);
        });
  };

  EXPECT_EQ(42, *e3());

  auto e4 = [&]() {
    return e3()
        >> Then([](int i) {
             return i + 1;
           })
        >> e3();
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

TEST(Task, CatchesFinally) {
  auto f = []() -> Task::Of<int>::Catches<RuntimeError> {
    return []() {
      return Finally([](expected<
                         void,
                         std::variant<
                             Stopped,
                             RuntimeError>>&& expected) {
        return Just(100);
      });
    };
  };

  auto e = [&f]() {
    return Raise(RuntimeError("error"))
        >> f();
  };

  static_assert(eventuals::tuple_types_unordered_equals_v<
                decltype(e())::ErrorsFrom<void, std::tuple<>>,
                std::tuple<>>);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<std::overflow_error>>,
          std::tuple<std::overflow_error>>);

  EXPECT_EQ(*e(), 100);
}

TEST(Task, CatchesRaiseFinallyInside) {
  auto f = []() -> Task::Of<int> {
    return []() {
      return Raise("error")
          >> Finally([](auto&& expected) {
               return Just(100);
             });
    };
  };

  auto e = [&f]() {
    return f();
  };

  static_assert(eventuals::tuple_types_unordered_equals_v<
                decltype(e())::ErrorsFrom<void, std::tuple<>>,
                std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(Task, TaskWithNonCopyable) {
  struct NonCopyable {
    NonCopyable(NonCopyable&&) = default;
    NonCopyable(const NonCopyable&) = delete;

    int x;
  };

  auto e = []() {
    return Task::Of<int>::With<NonCopyable>(
        NonCopyable{100},
        [](auto& non_copyable) {
          return Just(non_copyable.x);
        });
  };

  EXPECT_EQ(*e(), 100);
}

TEST(Task, TaskWithPtr) {
  int* x = new int(100);

  auto e = [&]() {
    return Task::Of<int>::With<int*>(
        x,
        [](int* non_copyable) {
          int value = *non_copyable;

          delete non_copyable;
          non_copyable = nullptr;

          return Just(value);
        });
  };

  EXPECT_EQ(*e(), 100);
  EXPECT_NE(x, nullptr);
}

TEST(Task, FailOnCallback) {
  auto e = [&]() -> Task::Of<int>::Raises<RuntimeError> {
    return [&]() {
      return Eventual<int>()
                 .raises<RuntimeError>()
                 .start([](auto& k) {
                   k.Fail(RuntimeError("error from start"));
                 })
                 .fail([&](auto& k, auto&& error) {
                   FAIL() << "test should not have failed";
                 })
          >> Then([](int) { return 1; })
          >> Eventual<int>()
                 .raises<RuntimeError>()
                 .start([&](auto& k, auto&& value) {
                   FAIL() << "test should not have started";
                 })
                 .stop([&](auto&) {
                   FAIL() << "test should not have stopped";
                 })
                 .fail([&](auto& k, auto&& error) {
                   k.Fail(std::forward<decltype(error)>(error));
                 });
    };
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  try {
    *e();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error from start");
  }
}

TEST(Task, FailTerminatedPropagate) {
  auto e = [&]() -> Task::Of<int>::Raises<RuntimeError> {
    return [&]() {
      return Eventual<int>()
                 .raises<RuntimeError>()
                 .start([](auto& k) {
                   k.Fail(RuntimeError("error from start"));
                 })
                 .fail([&](auto& k, auto&& error) {
                   FAIL() << "test should not have failed";
                 })
          >> Then([](int x) { return x + 1; });
    };
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  auto [future, k] = PromisifyForTest(e());
  k.Fail(RuntimeError("error"));

  try {
    future.get();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

TEST(Task, FailTerminatedCatch) {
  auto e = [&]() -> Task::Of<int>::Raises<
                     RuntimeError> {
    return [&]() {
      return Raise("error")
          >> Eventual<int>()
                 .raises<RuntimeError>()
                 .start([](auto& k) {
                   FAIL() << "test should not have started";
                 })
                 .fail([&](auto& k, auto&& error) {
                   EXPECT_EQ(error.what(), "error");
                   k.Fail(RuntimeError("error from fail"));
                 })
          >> Then([](int x) { return x + 1; });
    };
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  try {
    *e();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error from fail");
  }
}

TEST(Task, StopOnCallback) {
  auto e = [&]() -> Task::Of<int> {
    return [&]() {
      return Eventual<int>()
          .start([](auto& k) {
            k.Stop();
          })
          .stop([&](auto& k) {
            FAIL() << "test should not have stopped";
          });
    };
  };

  EXPECT_THROW(*e(), eventuals::Stopped);
}

TEST(Task, StopTerminated) {
  auto e = [&]() -> Task::Of<int> {
    return [&]() {
      return Eventual<int>()
          .start([](auto& k) {
            FAIL() << "test should not have started";
          })
          .stop([&](auto& k) {
            k.Stop();
          });
    };
  };

  auto [future, k] = PromisifyForTest(e());
  k.Stop();

  EXPECT_THROW(future.get(), eventuals::Stopped);
}

TEST(Task, Start) {
  auto e = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task::Of<int>> task;

  task.emplace(e());

  int result = 0;

  task->Start(
      GenerateTestTaskName(),
      [&](int x) {
        result = x;
      },
      []() {
        FAIL() << "test should not have failed";
      },
      []() {
        FAIL() << "test should not have stopped";
      });

  EXPECT_EQ(42, result);
}

TEST(Task, StartFuture) {
  auto e = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task::Of<int>> task;

  task.emplace(e());

  auto future = task->Start(GenerateTestTaskName());

  EXPECT_EQ(42, future.get());
}

TEST(Task, FailContinuation) {
  auto e = []() -> Task::Of<int>::Catches<RuntimeError> {
    return []() {
      return Finally([](expected<
                         void,
                         std::variant<
                             Stopped,
                             RuntimeError>>&& expected) {
        CHECK(std::holds_alternative<RuntimeError>(expected.error()));
        EXPECT_EQ(
            std::get<RuntimeError>(expected.error()).what(),
            "error");
        return 42;
      });
    };
  };

  std::optional<Task::Of<int>::Catches<RuntimeError>> task;

  task.emplace(e());

  task->Fail(
      GenerateTestTaskName(),
      RuntimeError("error"),
      [](int x) {
        EXPECT_EQ(x, 42);
      },
      []() {
        FAIL() << "test should not have failed";
      },
      []() {
        FAIL() << "test should not have stopped";
      });

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          std::decay_t<decltype(task.value())>::ErrorsFrom<
              void,
              std::tuple<>>,
          std::tuple<>>);
}

TEST(Task, StopContinuation) {
  auto e = []() -> Task::Of<int> {
    return [x = 42]() {
      return Just(x);
    };
  };

  std::optional<Task::Of<int>> task;

  task.emplace(e());

  bool stopped = false;

  task->Stop(
      GenerateTestTaskName(),
      [](int) {
        FAIL() << "test should not have succeeded";
      },
      []() {
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
        [](int x) {
          return Then([x](int value) {
            value += x;
            return std::to_string(value);
          });
        });
  };

  auto e = [&]() {
    return Just(10)
        >> task()
        >> Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  EXPECT_EQ(*e(), "201");
}

TEST(Task, FromToFail) {
  auto task = []() -> Task::From<int>::To<std::string> {
    return []() {
      return Then([](int x) {
        return std::to_string(x);
      });
    };
  };

  auto e = [&task]() {
    return Eventual<int>()
               .raises<RuntimeError>()
               .start([](auto& k) {
                 k.Fail(RuntimeError("error"));
               })
        >> Just(10)
        >> task()
        >> Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  try {
    *e();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

TEST(Task, FromToFailCatch) {
  auto task = []() -> Task::From<int>::To<
                       std::string>::Catches<RuntimeError> {
    return []() {
      return Catch()
                 .raised<RuntimeError>([](RuntimeError&& error) {
                   EXPECT_EQ(error.what(), "error");
                   return 10;
                 })
          >> Then([](int x) {
               return std::to_string(x);
             });
    };
  };

  auto e = [&task]() {
    return Eventual<int>()
               .raises<RuntimeError>()
               .start([](auto& k) {
                 k.Fail(RuntimeError("error"));
               })
        >> Just(10)
        >> task()
        >> Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), "101");
}

TEST(Task, FromToStop) {
  auto task = []() -> Task::From<int>::To<std::string> {
    return []() {
      return Then([](int x) {
        return std::to_string(x);
      });
    };
  };

  auto e = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Stop();
               })
        >> Just(10)
        >> task()
        >> Then([](std::string&& s) {
             s += "1";
             return std::move(s);
           });
  };

  EXPECT_THROW(*e(), eventuals::Stopped);
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
        >> g();
  };

  EXPECT_EQ("hello", *e());
}

TEST(Task, Failure) {
  auto e = []() -> Task::Of<std::string>::Raises<RuntimeError> {
    return Task::Failure("error");
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  try {
    *e();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

TEST(Task, Inheritance) {
  struct Base {
    virtual Task::Of<int>::Raises<RuntimeError> GetTask() = 0;
  };

  struct Sync : public Base {
    Task::Of<int>::Raises<RuntimeError> GetTask() override {
      return Task::Of<int>::Raises<RuntimeError>::Success<int>(10);
    }
  };

  struct Async : public Base {
    Task::Of<int>::Raises<RuntimeError> GetTask() override {
      return []() {
        return Just(20);
      };
    }
  };

  struct Failure : public Base {
    Task::Of<int>::Raises<RuntimeError> GetTask() override {
      return Task::Of<int>::Raises<RuntimeError>::Failure("error");
    }
  };

  auto f = []() -> Task::Of<void> {
    return []() {
      return Just();
    };
  };

  auto sync = [&]() {
    return f()
        >> Sync().GetTask();
  };

  auto async = [&]() {
    return f()
        >> Async().GetTask();
  };

  auto failure = [&]() {
    return f()
        >> Failure().GetTask();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(sync())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(async())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(failure())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_EQ(*sync(), 10);
  EXPECT_EQ(*async(), 20);

  try {
    *failure();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
}

TEST(Task, RaisesOut) {
  auto task = []() -> Task::Of<int>::Raises<RuntimeError> {
    return []() {
      return Eventual<int>()
          .raises<RuntimeError>()
          .start([](auto& k) {
            k.Fail(RuntimeError("error"));
          });
    };
  };

  auto e = [&]() {
    return task()
        >> Eventual<int>()
               .raises<RuntimeError>()
               .start([](auto&, auto&&) {})
               .fail([](auto& k, auto&& error) {
                 k.Fail(std::move(error));
               });
  };

  try {
    *e();
  } catch (const RuntimeError& error) {
    EXPECT_EQ(error.what(), "error");
  }
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
        >> Then([](const int& value) {
             return value + 10;
           });
  };

  auto [future, k] = PromisifyForTest(e());

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

  auto [future, k] = PromisifyForTest(e());

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
        >> Then([](int& v) {
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
        >> Then([](int& v) {
             v += 100;
           });
  };

  *e1();

  EXPECT_EQ(110, x);
}

TEST(Task, RaisesWith) {
  auto e = []() {
    return Task::Of<int>::Raises<RuntimeError>::With<int, std::string>(
        42,
        "hello world",
        [](auto i, auto s) {
          EXPECT_EQ(s, "hello world");
          return Eventual<int>()
              .raises<RuntimeError>()
              .start([i = i](auto& k) {
                return k.Start(i);
              });
        });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_EQ(42, *e());
}

TEST(Task, RaisesGeneralError) {
  auto task = []() -> Task::Of<int>::Raises<TypeErasedError> {
    return []() {
      return Eventual<int>()
          .raises<RuntimeError>()
          .start([](auto& k) {
            k.Fail(RuntimeError("runtime error"));
          });
    };
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(task())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<TypeErasedError>>);

  try {
    *task();
  } catch (const RuntimeError& error) {
    FAIL() << "error of 'RuntimeError' type shouldn't be thrown";
  } catch (const TypeErasedError& error) {
    EXPECT_EQ(error.what(), "runtime error");
  }
}

} // namespace
} // namespace eventuals::test
