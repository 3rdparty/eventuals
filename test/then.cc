#include "stout/then.h"
#include <thread>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Eventual;
using stout::eventuals::Interrupt;
using stout::eventuals::stop;
using stout::eventuals::succeed;
using stout::eventuals::Terminate;
using stout::eventuals::Then;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(ThenTest, Succeed) {
    auto e = [](auto s) {
        return Eventual<std::string>()
            .context(std::move(s))
            .start([](auto& s, auto& k) { succeed(k, std::move(s)); });
    };

    auto c = [&]() {
        return Eventual<int>().context(1).start([](auto& value, auto& k) {
            auto thread =
                std::thread([&value, &k]() mutable { succeed(k, value); });
            thread.detach();
        }) | [](int i) { return i + 1; } |
               Then([&](auto&& i) { return e("then"); });
    };

    EXPECT_EQ("then", *c());
}

TEST(ThenTest, Fail) {
    auto e = [](auto s) {
        return Eventual<std::string>().context(s).start(
            [](auto& s, auto& k) { succeed(k, std::move(s)); });
    };

    auto c = [&]() {
        return Eventual<int>().start([](auto& k) {
            auto thread = std::thread([&k]() mutable { fail(k, "error"); });
            thread.detach();
        }) | [](int i) { return i + 1; } |
               Then([&](auto&& i) { return e("then"); });
    };

    EXPECT_THROW(*c(), FailedException);
}

TEST(ThenTest, Interrupt) {
    // Using mocks to ensure start is only called once.
    MockFunction<void()> start;

    auto                 e = [&](auto) {
        return Eventual<std::string>()
            .start([&](auto&) { start.Call(); })
            .interrupt([](auto& k) { stop(k); });
    };

    auto c = [&]() {
        return Eventual<int>().start([](auto& k) { succeed(k, 0); }) |
               [](int i) { return i + 1; } |
               Then([&](auto&& i) { return e("then"); });
    };

    auto [future, t] = Terminate(c());

    Interrupt interrupt;

    t.Register(interrupt);

    EXPECT_CALL(start, Call()).WillOnce([&]() { interrupt.Trigger(); });

    t.Start();

    EXPECT_THROW(future.get(), StoppedException);
}
