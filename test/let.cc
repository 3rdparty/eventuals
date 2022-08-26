#include "eventuals/let.hh"

#include "event-loop-test.hh"
#include "eventuals/event-loop.hh"
#include "eventuals/just.hh"
#include "eventuals/promisify.hh"
#include "eventuals/then.hh"
#include "eventuals/timer.hh"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

class LetTest : public EventLoopTest {};

TEST_F(LetTest, Let) {
  struct Foo {
    int i;
  };

  auto e = []() {
    return Just(Foo{41})
        >> Then(Let([](Foo& foo) {
             return Then([&]() {
                      foo.i += 1;
                    })
                 >> Timer(std::chrono::milliseconds(1))
                 >> Then([&]() {
                      return foo.i;
                    });
           }));
  };

  EXPECT_EQ(42, *e());
}

} // namespace
} // namespace eventuals::test
