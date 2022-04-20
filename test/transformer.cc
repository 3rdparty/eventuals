#include "eventuals/transformer.h"

#include <string>

#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/interrupt.h"
#include "eventuals/iterate.h"
#include "eventuals/let.h"
#include "eventuals/map.h"
#include "eventuals/stream.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/expect-throw-what.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::MockFunction;

TEST(Transformer, Succeed) {
  auto transformer = []() {
    return Transformer::From<int>::To<std::string>(
        []() {
          return Map([](int&& x) {
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Iterate({100})
        | transformer()
        | Map([](std::string s) {
             return s;
           })
        | Collect<std::vector<std::string>>();
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
          return Map([&](int&& x) {
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
        | transformer()
        | Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        | Collect<std::vector<std::string>>();
  };

  EXPECT_THROW(*e(), eventuals::StoppedException);
}

TEST(Transformer, Fail) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = [&]() {
    return Transformer::From<int>::To<std::string>(
        [&]() {
          return Map([&](int&& x) {
            map_start.Call();
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Stream<int>()
               .raises<std::runtime_error>()
               .next([](auto& k) {
                 k.Fail(std::runtime_error("error"));
               })
        | transformer()
        | Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        | Collect<std::vector<std::string>>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THROW_WHAT(*e(), "error");
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
          return Map([&](int&& x) {
            map_start.Call();
            return std::to_string(x);
          });
        });
  };

  auto e = [&]() {
    return Stream<int>()
               .interruptible()
               .begin([](auto& k, Interrupt::Handler& handler) {
                 handler.Install([&]() {
                   k.Stop();
                 });
                 k.Begin();
               })
               .next([&](auto&) {
                 next.Call();
               })
               .done([&](auto&) {
                 done.Call();
               })
        | transformer()
        | Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        | Collect<std::vector<std::string>>();
  };

  Interrupt interrupt;

  auto [future, k] = Terminate(e());

  k.Register(interrupt);

  k.Start();

  interrupt.Trigger();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}

TEST(Transformer, PropagateStop) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = []() {
    return Transformer::From<int>::To<std::string>([]() {
      return Map(Let([](auto& i) {
        return Eventual<std::string>([](auto& k) {
          k.Stop();
        });
      }));
    });
  };

  auto e = [&]() {
    return Iterate({100})
        | transformer()
        | Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        | Collect<std::vector<std::string>>();
  };

  EXPECT_THROW(*e(), eventuals::StoppedException);
}

TEST(Transformer, PropagateFail) {
  MockFunction<void()> map_start;

  EXPECT_CALL(map_start, Call())
      .Times(0);

  auto transformer = []() {
    return Transformer::From<int>::To<std::string>::Raises<std::runtime_error>(
        []() {
          return Map(Let([](auto& i) {
            return Eventual<std::string>()
                .raises<std::runtime_error>()
                .start([](auto& k) {
                  k.Fail(std::runtime_error("error"));
                });
          }));
        });
  };

  auto e = [&]() {
    return Iterate({100})
        | transformer()
        | Map([&](std::string s) {
             map_start.Call();
             return s;
           })
        | Collect<std::vector<std::string>>();
  };

  static_assert(
      eventuals::tuple_types_unordered_equals_v<
          typename decltype(e())::template ErrorsFrom<void, std::tuple<>>,
          std::tuple<std::runtime_error>>);

  EXPECT_THROW_WHAT(*e(), "error");
}

} // namespace
} // namespace eventuals::test
