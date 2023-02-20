#include "eventuals/on-ended.h"

#include <vector>

#include "eventuals/collect.h"
#include "eventuals/expected.h"
#include "eventuals/finally.h"
#include "eventuals/iterate.h"
#include "eventuals/promisify.h"
#include "eventuals/then.h"
#include "eventuals/timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;
using testing::MockFunction;

class OnEndedTest : public EventLoopTest {};

TEST_F(OnEndedTest, OnlyOnceAndAsynchronous) {
  MockFunction<void()> ended;

  EXPECT_CALL(ended, Call())
      .Times(1);

  auto e = [&]() {
    return Iterate({1, 2, 3})
        >> OnEnded([&]() {
             ended.Call();
             return Timer(std::chrono::milliseconds(10))
                 >> Finally([&](expected<void, std::exception_ptr>&& e) {
                      // EXPECT_TRUE(e);
                    });
           })
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3));
}

} // namespace
} // namespace eventuals::test
