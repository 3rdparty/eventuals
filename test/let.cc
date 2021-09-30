#include "stout/let.h"

#include "gtest/gtest.h"
#include "stout/event-loop.h"
#include "stout/just.h"
#include "stout/terminal.h"
#include "stout/then.h"
#include "stout/timer.h"
#include "test/event-loop-test.h"

using stout::eventuals::EventLoop;
using stout::eventuals::Just;
using stout::eventuals::Let;
using stout::eventuals::Then;
using stout::eventuals::Timer;

class LetTest : public EventLoopTest {};

TEST_F(LetTest, Let) {
  struct Foo {
    int i;
  };

  auto e = []() {
    return Just(Foo{41})
        | Then(Let([](auto& foo) {
             return Then([&]() {
                      foo.i += 1;
                    })
                 | Timer(std::chrono::milliseconds(1))
                 | Then([&]() {
                      return foo.i;
                    });
           }));
  };

  auto [future, k] = Terminate(e());

  k.Start();

  EventLoop::Default().Run();

  EXPECT_EQ(42, future.get());
}
