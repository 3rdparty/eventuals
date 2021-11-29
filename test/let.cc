#include "eventuals/let.h"

#include "event-loop-test.h"
#include "eventuals/event-loop.h"
#include "eventuals/just.h"
#include "eventuals/terminal.h"
#include "eventuals/then.h"
#include "eventuals/timer.h"
#include "gtest/gtest.h"

using eventuals::EventLoop;
using eventuals::Just;
using eventuals::Let;
using eventuals::Then;
using eventuals::Timer;

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

  EventLoop::Default().RunUntil(future);

  EXPECT_EQ(42, future.get());
}
