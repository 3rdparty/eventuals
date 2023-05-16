#include "eventuals/catch.h"

#include <iostream>
#include <string>

#include "eventuals/conditional.h"
#include "eventuals/expected.h"
#include "eventuals/interrupt.h"
#include "eventuals/just.h"
#include "eventuals/raise.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;

TEST(CatchTest, RaisedRuntimeError) {
  auto e = []() {
    return Just(1)
        >> Raise(RuntimeError("message"))
        >> Catch()
               .raised<RuntimeError>([](RuntimeError&& error) {
                 EXPECT_EQ(error.what(), "message");
                 return Just(100);
               });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(CatchTest, ChildException) {
  struct MyError : public Error {
    std::string what() const noexcept override {
      return "child exception";
    }
  };

  auto e = []() {
    return Just(1)
        >> Raise(MyError{})
        >> Catch()
               .raised<TypeErasedError>(
                   [](TypeErasedError&& error) {
                     EXPECT_EQ("child exception", error.what());
                     return Just(100);
                   });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(CatchTest, All) {
  auto e = []() {
    return Just(500)
        >> Raise(RuntimeError("10"))
        >> Catch()
               .all([](std::variant<RuntimeError>&& error) {
                 EXPECT_EQ(std::get<RuntimeError>(error).what(), "10");
                 return 100;
               })
        >> Then([](int value) {
             return value;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(CatchTest, AllRaisedOneException) {
  struct MyError : public Error {
    std::string what() const noexcept override {
      return "child exception";
    }
  };

  auto e = []() {
    return Just(500)
        >> Raise(RuntimeError("runtime_error"))
        >> Raise(MyError{})
        >> Catch()
               .raised<MyError>([](MyError&& error) {
                 FAIL() << "Encountered unexpected matched raised";
               })
               .all([](std::variant<RuntimeError>&& error) {
                 EXPECT_EQ(
                     std::get<RuntimeError>(error).what(),
                     "runtime_error");
                 return 100;
               })
        >> Then([](int value) {
             return value;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(CatchTest, UnexpectedRaise) {
  struct MyError : public Error {
    std::string what() const noexcept override {
      return "child exception";
    }
  };


  auto f = []() -> expected<int, MyError> {
    return make_unexpected(MyError{});
  };

  auto e = [&]() {
    return f()
        >> Catch()
               .raised<MyError>([](MyError&& error) {
                 EXPECT_EQ("child exception", error.what());
                 return 100;
               });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(CatchTest, UnexpectedAll) {
  struct MyError : public Error {
    std::string what() const noexcept override {
      return "child exception";
    }
  };

  auto f = []() -> expected<int, MyError> {
    return make_unexpected(MyError{});
  };

  auto e = [&]() {
    return f()
        >> Catch()
               .all([](std::variant<MyError>&& error) {
                 EXPECT_EQ(std::get<MyError>(error).what(), "child exception");

                 return 100;
               });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(*e(), 100);
}

TEST(CatchTest, NoExactHandler) {
  struct MyError : public Error {
    std::string what() const noexcept override {
      return "child exception";
    }
  };

  auto e = []() {
    return Just(1)
        >> Raise(std::string("error"))
        >> Catch()
               .raised<MyError>([](MyError&& error) {
                 FAIL() << "Encountered unexpected matched raised";
               });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_THROW(*e(), RuntimeError);
}

TEST(CatchTest, ReRaise) {
  auto e = []() {
    return Just(1)
        >> Raise("10")
        >> Catch()
               .raised<RuntimeError>([](RuntimeError&& error) {
                 EXPECT_EQ(error.what(), "10");
                 return Raise("1");
               })
        >> Then([](int) {
             return 200;
           })
        >> Catch()
               .raised<RuntimeError>([](RuntimeError&& error) {
                 EXPECT_EQ(error.what(), "1");
                 return Just(10);
               })
        >> Then([](int value) {
             return value;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(10, *e());
}

TEST(CatchTest, VoidPropagate) {
  auto e = []() {
    return Just("some string")
        >> Then([](const char* i) {
             return;
           })
        >> Raise("error")
        >> Catch()
               .raised<TypeErasedError>([](TypeErasedError&& error) {
                 EXPECT_EQ(error.what(), "error");
                 // MUST RETURN VOID HERE!
               })
        >> Then([](/* MUST TAKE VOID HERE! */) {
             return 100;
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  EXPECT_EQ(100, *e());
}

TEST(CatchTest, Interrupt) {
  auto e = []() {
    return Just(1)
        >> Raise(RuntimeError("message"))
        >> Catch()
               .raised<RuntimeError>([](RuntimeError&& error) {
                 EXPECT_EQ(error.what(), "message");
                 return Just(100);
               })
        >> Then([](int i) {
             return std::to_string(i);
           });
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(e())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  auto [future, k] = PromisifyForTest(e());

  Interrupt interrupt;

  k.Register(interrupt);

  k.Start();

  EXPECT_EQ(future.get(), "100");
}

TEST(CatchTest, RaiseFromCatch) {
  auto all = []() {
    return Just(1)
        >> Raise("10")
        >> Catch()
               .all([](auto&& error) {
                 return Just(10) >> Raise("1");
               });
  };

  auto raised = []() {
    return Just(1)
        >> Raise("10")
        >> Catch()
               .raised<RuntimeError>([](auto&& error) {
                 return Just(10) >> Raise("1");
               });
  };

  // Be careful there!
  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(all())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<>>);

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          decltype(raised())::ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);
}


} // namespace
} // namespace eventuals::test
