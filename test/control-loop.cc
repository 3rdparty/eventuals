#include "eventuals/control-loop.h"

#include <optional>

#include "eventuals/collect.h"
#include "eventuals/eventual.h"
#include "eventuals/iterate.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/pipe.h"
#include "eventuals/promisify.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Collect;
using eventuals::ControlLoop;
using eventuals::Eventual;
using eventuals::Interrupt;
using eventuals::Iterate;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Pipe;

using testing::ElementsAre;
using testing::MockFunction;

TEST(ControlLoop, SimplePipeHandling) {
  Pipe<std::string> pipe;

  ControlLoop control_loop("Simple pipe writing", [&]() {
    return Iterate({0, 1, 2, 3, 4})
        >> Map([&](int i) {
             return pipe.Write(std::to_string(i));
           })
        >> Loop();
  });

  *control_loop.Wait();

  *pipe.Close();

  auto collect_from_pipe = [&]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(
      *collect_from_pipe(),
      ElementsAre("0", "1", "2", "3", "4"));
}

TEST(ControlLoop, Interrupt) {
  MockFunction<void()> start;

  EXPECT_CALL(start, Call())
      .Times(1);

  ControlLoop control_loop("Interrupt eventual", [&]() {
    return Eventual<void>()
        .interruptible()
        .start([&](
                   auto& k,
                   std::optional<Interrupt::Handler>& handler) {
          start.Call();
          EXPECT_TRUE(handler->Install([&k]() {
            k.Stop();
          }));
        });
  });

  control_loop.Interrupt();
  *control_loop.Wait();
}
