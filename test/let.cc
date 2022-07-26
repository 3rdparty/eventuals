#include "eventuals/let.h"

#include "event-loop-test.h"
#include "eventuals/event-loop.h"
#include "eventuals/just.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "eventuals/timer.h"
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

  EXPECT_EQ(42, *e());
}

} // namespace
} // namespace eventuals::test
