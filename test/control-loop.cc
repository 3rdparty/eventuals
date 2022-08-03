#include "eventuals/control-loop.h"

#include "eventuals/collect.h"
#include "eventuals/loop.h"
#include "eventuals/map.h"
#include "eventuals/pipe.h"
#include "eventuals/repeat.h"
#include "eventuals/until.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using eventuals::Collect;
using eventuals::ControlLoop;
using eventuals::Loop;
using eventuals::Map;
using eventuals::Pipe;
using eventuals::Repeat;
using eventuals::Until;

using testing::ElementsAre;

TEST(ControlLoop, SimplePipeHandling) {
  Pipe<std::string> pipe;

  ControlLoop control_loop("Simple pipe writing", [&]() {
    return Repeat([&, i = 0]() mutable {
             return i++;
           })
        >> Until([&](auto& i) {
             return i == 5;
           })
        >> Map([&](auto&& i) {
             return pipe.Write(std::to_string(i));
           })
        >> Loop();
  });

  *control_loop.Wait();

  *pipe.Close();

  auto read_from_pipe = [&]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(
      *read_from_pipe(),
      ElementsAre("0", "1", "2", "3", "4"));
}
