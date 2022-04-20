#include "eventuals/pipe.h"

#include <string>
#include <thread>

#include "eventuals/collect.h"
#include "eventuals/terminal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals {
namespace {
using testing::ElementsAre;

TEST(Pipe, UniqueValue) {
  Pipe<int> pipe;

  *pipe.Write(1);
  *pipe.Close();

  auto e = [&pipe]() {
    return pipe.Read()
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e(), ElementsAre(1));
}


TEST(Pipe, Values) {
  Pipe<int> pipe;

  std::thread t([&pipe]() {
    for (size_t i = 1; i <= 5; ++i) {
      *pipe.Write(i);
    }
    *pipe.Close();
  });

  auto e = [&pipe]() {
    return pipe.Read()
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3, 4, 5));

  t.join();
}


TEST(Pipe, Close) {
  Pipe<int> pipe;

  *pipe.Write(1);
  *pipe.Write(2);
  *pipe.Close();
  *pipe.Write(3);

  auto e = [&pipe]() {
    return pipe.Read()
        | Collect<std::vector<int>>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2));
}


TEST(Pipe, Size) {
  Pipe<std::string> pipe;

  *pipe.Write(std::string{"Hello"});
  *pipe.Write(std::string{" world!"});
  *pipe.Close();

  auto e = [&pipe]() {
    return pipe.Read()
        | Collect<std::vector<std::string>>();
  };

  EXPECT_EQ(*pipe.Size(), 2);

  EXPECT_THAT(
      *e(),
      ElementsAre(std::string{"Hello"}, std::string{" world!"}));
}
} // namespace
} // namespace eventuals
