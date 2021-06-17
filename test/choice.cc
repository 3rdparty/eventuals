#include <thread>

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/choice.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Choice;
using stout::eventuals::Eventual;
using stout::eventuals::succeed;

using stout::eventuals::FailedException;
using stout::eventuals::StoppedException;

using testing::MockFunction;

TEST(ChoiceTest, Yes)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .context(1)
      .start([](auto& value, auto& k) {
        auto thread = std::thread(
            [&value, &k]() mutable {
              succeed(k, value);
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Choice<std::string>([&]() { return e(); })
         .start([](auto& k, auto& yes, auto&& i) {
           if (i > 1) {
             succeed(yes);
           } else {
             succeed(k, "no");
           }
         }));
  };

  EXPECT_EQ("yes", *c());
}


TEST(ChoiceTest, No)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .context(0)
      .start([](auto& value, auto& k) {
        auto thread = std::thread(
            [&value, &k]() mutable {
              succeed(k, value);
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Choice<std::string>([&]() { return e(); })
         .start([](auto& k, auto& yes, auto&& i) {
           if (i > 1) {
             succeed(yes);
           } else {
             succeed(k, "no");
           }
         }));
  };

  EXPECT_EQ("no", *c());
}


TEST(ChoiceTest, Maybe)
{
  auto yes = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
      });
  };

  auto maybe = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "maybe");
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .context(1)
      .start([](auto& value, auto& k) {
        auto thread = std::thread(
            [&value, &k]() mutable {
              succeed(k, value);
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Choice<std::string>(
             [&]() { return yes(); },
             [&]() { return maybe(); })
         .start([](auto& k, auto& yes, auto& maybe, auto&& i) {
           if (i > 1) {
             succeed(maybe);
           } else {
             succeed(k, "no");
           }
         }));
  };

  EXPECT_EQ("maybe", *c());
}


TEST(ChoiceTest, FailBeforeStart)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .start([](auto& k) {
        auto thread = std::thread(
            [&k]() mutable {
              fail(k, "error");
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Choice<std::string>([&]() { return e(); })
         .start([](auto& k, auto& yes, auto&& i) {
           if (i > 1) {
             succeed(yes);
           } else {
             succeed(k, "no");
           }
         }));
  };

  EXPECT_THROW(*c(), FailedException);
}


TEST(ChoiceTest, FailAfterStart)
{
  auto e = []() {
    return Eventual<std::string>()
      .start([](auto& k) {
        succeed(k, "yes");
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .context(0)
      .start([](auto& value, auto& k) {
        auto thread = std::thread(
            [&value, &k]() mutable {
              succeed(k, value);
            });
        thread.detach();
      })
      | [](int i) { return i + 1; }
      | (Choice<std::string>([&]() { return e(); })
         .start([](auto& k, auto& yes, auto&& i) {
           if (i > 1) {
             succeed(yes);
           } else {
             fail(k, "error");
           }
         }));
  };

  EXPECT_THROW(*c(), FailedException);
}


TEST(ChoiceTest, Interrupt)
{
  // Using mocks to ensure start is only called once.
  MockFunction<void()> start;

  auto e = [&]() {
    return Eventual<std::string>()
      .start([&](auto&) {
        start.Call();
      })
      .interrupt([](auto& k) {
        stop(k);
      });
  };

  auto c = [&]() {
    return Eventual<int>()
      .start([](auto& k) {
        succeed(k, 0);
      })
      | [](int i) { return i + 1; }
      | (Choice<std::string>([&]() { return e(); })
         .start([](auto& k, auto& yes, auto&&) {
           succeed(yes);
         }));
  };

  auto t = eventuals::TaskFrom(c());

  EXPECT_CALL(start, Call())
    .WillOnce([&]() {
      t.Interrupt();
    });

  t.Start();

  EXPECT_THROW(t.Wait(), StoppedException);
}
