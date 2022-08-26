#include "eventuals/pipe.h"

#include <string>
#include <thread>

#include "eventuals/collect.h"
#include "eventuals/promisify.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

using testing::ElementsAre;

TEST(Pipe, UniqueValue) {
  Pipe<int> pipe;

  *pipe.Write(1);
  *pipe.Close();

  auto e = [&pipe]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1));
}


TEST(Pipe, ReadWriteFromDifferentThreads) {
  Pipe<int> pipe;

  std::thread t([&pipe]() {
    for (size_t i = 1; i <= 5; ++i) {
      *pipe.Write(i);
    }
    *pipe.Close();
  });

  auto e = [&pipe]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2, 3, 4, 5));

  t.join();
}

TEST(Pipe, Size) {
  Pipe<std::string> pipe;

  *pipe.Write("Hello");
  *pipe.Write(" world!");
  *pipe.Close();

  auto e = [&pipe]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };

  EXPECT_EQ(*pipe.Size(), 2);
  EXPECT_THAT(*e(), ElementsAre("Hello", " world!"));
}

TEST(Pipe, Close) {
  Pipe<int> pipe;

  *pipe.Write(1);
  *pipe.Write(2);
  ASSERT_EQ(*pipe.Size(), 2);

  // Close the pipe, preventing more values from being written.
  ASSERT_FALSE(*pipe.IsClosed());
  *pipe.Close();
  EXPECT_TRUE(*pipe.IsClosed());

  // Values written to a closed pipe are silently dropped.
  *pipe.Write(3);
  EXPECT_EQ(*pipe.Size(), 2);

  auto e = [&pipe]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };

  EXPECT_THAT(*e(), ElementsAre(1, 2));
}

} // namespace
} // namespace eventuals::test
