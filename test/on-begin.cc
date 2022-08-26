#include "eventuals/on-begin.hh"

#include <vector>

#include "eventuals/collect.hh"
#include "eventuals/expected.hh"
#include "eventuals/finally.hh"
#include "eventuals/iterate.hh"
#include "eventuals/promisify.hh"
#include "eventuals/then.hh"
#include "eventuals/timer.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.hh"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::MockFunction;

class OnBeginTest : public EventLoopTest {};

TEST_F(OnBeginTest, OnlyOnceAndAsynchronous) {
  MockFunction<void()> begin;

  EXPECT_CALL(begin, Call())
      .Times(1);

  auto e = [&]() {
    return Iterate({1, 2, 3})
        >> OnBegin([&]() {
             begin.Call();
             return Timer(std::chrono::milliseconds(10))
                 >> Finally([&](expected<void, std::exception_ptr>&& e) {
                      EXPECT_TRUE(e);
                    });
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3));
}

} // namespace
} // namespace eventuals::test
