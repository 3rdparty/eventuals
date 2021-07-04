

#include "gmock/gmock.h"

#include "gtest/gtest.h"

#include "stout/just.h"
#include "stout/lambda.h"
#include "stout/task.h"

namespace eventuals = stout::eventuals;

using stout::eventuals::Just;
using stout::eventuals::Lambda;
using stout::eventuals::Task;

TEST(TaskTest, Succeed)
{
  auto e1 = []() -> Task<int> {
    return Just(42);
  };

  auto e2 = [&]() {
    return e1()
      | Lambda([](int i) {
        return i + 1; 
      })
      | e1();
  };

  EXPECT_EQ(42, *e2());
}
