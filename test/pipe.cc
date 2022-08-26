#include "eventuals/pipe.hh"

#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "eventuals/collect.hh"
#include "eventuals/promisify.hh"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.hh"

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

TEST(Pipe, WaitForClosedAndEmpty) {
  Pipe<int> pipe;

  *pipe.Write(1);
  *pipe.Write(2);
  *pipe.Close();
  ASSERT_EQ(*pipe.Size(), 2);
  ASSERT_TRUE(*pipe.IsClosed());

  auto [future, k] = PromisifyForTest(pipe.WaitForClosedAndEmpty());
  k.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  // Drain the pipe of values.
  auto e = [&pipe]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };
  EXPECT_THAT(*e(), ElementsAre(1, 2));

  // WaitForClosedAndEmpty now returns.
  EXPECT_EQ(
      std::future_status::ready,
      future.wait_for(std::chrono::seconds(0)));
}

} // namespace
} // namespace eventuals::test
