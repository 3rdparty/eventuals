#include "eventuals/then.h"

#include <thread>

#include "eventuals/just.h"
#include "eventuals/timeout-after.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

using testing::MockFunction;
using testing::StrEq;
using testing::ThrowsMessage;

TEST(ThenTest, Succeed) {
  auto e = [](auto s) {
    return Eventual<std::string>()
        .context(std::move(s))
        .start([](auto& s, auto& k) {
          k.Start(std::move(s));
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .context(1)
               .start([](auto& value, auto& k) {
                 auto thread = std::thread(
                     [&value, &k]() mutable {
                       k.Start(value);
                     });
                 thread.detach();
               })
        | Then([](int i) { return i + 1; })
        | Then([&](auto&& i) {
             return e("then");
           });
  };

  EXPECT_EQ("then", *c());
}

TEST(ThenTest, SucceedVoid) {
  bool ran = false;
  auto e = [&]() {
    return Just()
        | Then([&]() {
             ran = true;
           });
  };

  // Then() doesn't return anything when its callable (in this case, its
  // lambda) doesn't return anything.
  static_assert(std::is_void_v<decltype(*e())>);
  *e();
  EXPECT_TRUE(ran);
}

TEST(ThenTest, Fail) {
  auto e = [](auto s) {
    return Eventual<std::string>()
        .context(s)
        .start([](auto& s, auto& k) {
          k.Start(std::move(s));
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .raises<std::runtime_error>()
               .start([](auto& k) {
                 auto thread = std::thread(
                     [&k]() mutable {
                       k.Fail(std::runtime_error("error"));
                     });
                 thread.detach();
               })
        | Then([](int i) { return i + 1; })
        | Then([&](auto&& i) {
             return e("then");
           });
  };

  EXPECT_THAT(
      [&]() { *c(); },
      ThrowsMessage<std::runtime_error>(StrEq("error")));
}


TEST(ThenTest, Interrupt) {
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&](auto) {
    return Eventual<std::string>()
        .interruptible()
        .start([&](auto& k, auto& handler) {
          handler->Install([&k]() {
            k.Stop();
          });
          start.Call();
        });
  };

  auto c = [&]() {
    return Eventual<int>()
               .start([](auto& k) {
                 k.Start(0);
               })
        | Then([](int i) { return i + 1; })
        | Then([&](auto&& i) {
             return e("then");
           });
  };

  auto [future, k] = PromisifyForTest(c());

  Interrupt interrupt;

  k.Register(interrupt);

  EXPECT_CALL(start, Call())
      .WillOnce([&]() {
        interrupt.Trigger();
      });

  k.Start();

  EXPECT_THROW(future.get(), eventuals::StoppedException);
}


TEST(ThenTest, Move) {
  auto e = []() {
    return TimeoutAfter(std::chrono::seconds(1), Just(42));
  };

  EXPECT_EQ(42, *e());
}

} // namespace
} // namespace eventuals::test
