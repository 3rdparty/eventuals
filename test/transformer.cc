#include "eventuals/transformer.h"

#include <string>

#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(Transformer, Succeed) {
  auto transformer = []() {
    return Transformer::From<int>::To<std::string>(
        []() {
          return Map([](int x) {
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Iterate({100})
        >> transformer()
        >> Map([](std::string s) {
             return s;
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre("100"));
}

TEST(Transformer, Stop) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = [&]() {
    return Transformer::From<int>::To<std::string>(
        [&]() {
          return Map([&](int x) {
            map_start.Call();
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Stream<int>()
               .next([](auto& k) {
                 k.Stop();
               })
        >> transformer()
        >> Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        >> Collect<std::vector>();
  };

  EXPECT_THROW(*e(), eventuals::Stopped);
}

TEST(Transformer, Fail) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = [&]() {
    return Transformer::From<int>::To<std::string>(
        [&]() {
          return Map([&](int x) {
            map_start.Call();
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Stream<int>()
               .raises<RuntimeError>()
               .next([](auto& k) {
                 k.Fail(RuntimeError("error"));
               })
        >> transformer()
        >> Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("error")));
}

TEST(Transformer, Interrupt) {
  MockFunction<void()> map_start, next, done;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  EXPECT_CALL(next, Call())
      .Times(1);

  EXPECT_CALL(done, Call())
      .Times(0);

  auto transformer = [&]() {
    return Transformer::From<int>::To<std::string>(
        [&]() {
          return Map([&](int x) {
            map_start.Call();
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, auto& handler) {
                 CHECK(handler) << "Test expects interrupt to be registered";
                 EXPECT_TRUE(handler->Install([&]() {
                   k.Stop();
                 }));
                 k.Begin();
               })
               .next([&](auto&, auto&) {
                 next.Call();
               })
               .done([&](auto&, auto&) {
                 done.Call();
               })
        >> transformer()
        >> Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        >> Collect<std::vector>();
  };

  Interrupt interrupt;

  auto [future, k] = PromisifyForTest(e());

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::Stopped);
}

TEST(Transformer, PropagateStop) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = []() {
    return Transformer::From<int>::To<std::string>([]() {
      return Map(Let([](int& i) {
        return Eventual<std::string>([](auto& k) {
          k.Stop();
        });
      }));
    });
  };

  auto e = [&]() {
    return Iterate({100})
        >> transformer()
        >> Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        >> Collect<std::vector>();
  };

  EXPECT_THROW(*e(), eventuals::Stopped);
}

TEST(Transformer, PropagateFail) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = []() {
    return Transformer::From<int>::To<std::string>::Raises<RuntimeError>(
        []() {
          return Map(Let([](int& i) {
            return Eventual<std::string>()
                .raises<RuntimeError>()
                .start([](auto& k) {
                  k.Fail(RuntimeError("error"));
                });
          }));
        });
  };

  auto e = [&]() {
    return Iterate({100})
        >> transformer()
        >> Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<RuntimeError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<RuntimeError>(StrEq("error")));
}

TEST(Transformer, RaisesGeneralError) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = []() {
    return Transformer::From<int>::To<std::string>::Raises<TypeErasedError>(
        []() {
          return Map(Let([](int& i) {
            return Eventual<std::string>()
                .raises<RuntimeError>()
                .start([](auto& k) {
                  k.Fail(RuntimeError("runtime error"));
                });
          }));
        });
  };

  auto e = [&]() {
    return Iterate({100})
        >> transformer()
        >> Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        >> Collect<std::vector>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<TypeErasedError>>);

  EXPECT_THAT(
      [&]() { *e(); },
      ThrowsMessage<TypeErasedError>(StrEq("runtime error")));
}

} // namespace
} // namespace eventuals::test
