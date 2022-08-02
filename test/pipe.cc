#include "eventuals/pipe.h"

#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "eventuals/collect.h"
#include "eventuals/promisify.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

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
        >> Collect<std::vector>();
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
        >> Collect<std::vector>();
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
        >> Collect<std::vector>();
  };

  EXPECT_EQ(*pipe.Size(), 2);

  EXPECT_THAT(
      *e(),
      ElementsAre(std::string{"Hello"}, std::string{" world!"}));
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
      future.wait_for(std::chrono::milliseconds(100)));

  // Drain the pipe of values.
  auto e = [&pipe]() {
    return pipe.Read()
        >> Collect<std::vector>();
  };
  EXPECT_THAT(*e(), ElementsAre(1, 2));

  // WaitForClosedAndEmpty now returns.
  EXPECT_NE(
      std::future_status::timeout,
      future.wait_for(std::chrono::milliseconds(100)));
}

} // namespace
} // namespace eventuals::test
