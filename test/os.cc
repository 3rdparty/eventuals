#include "eventuals/os.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eventuals::test {
namespace {

#ifndef _WIN32

using eventuals::os::Thread;
using testing::MockFunction;

void foo() {
  // Do nothing.
}

TEST(Thread, NotJoinable) {
  Thread t;
  EXPECT_FALSE(t.is_joinable());
}

TEST(Thread, Joinable) {
  Thread t{&foo, "thread_name"};
  EXPECT_TRUE(t.is_joinable());
  t.join();
  EXPECT_FALSE(t.is_joinable());
}

TEST(Thread, SetStackSize) {
  Thread t{
      []() {
        EXPECT_EQ(os::GetStackInfo().size, Bytes(16'777'216));
      },
      "thread_name",
      Bytes(16'777'216)};
  EXPECT_TRUE(t.is_joinable());
  t.join();
  EXPECT_FALSE(t.is_joinable());
}

TEST(Thread, LambdaThatCapturesEverything) {
  MockFunction<void()> start;

  EXPECT_CALL(start, Call())
      .Times(1);

  Thread t{[&]() {
             start.Call();
           },
           "thread_name"};

  t.join();
}

TEST(Thread, LambdaThatCapturesNothing) {
  Thread t{[]() {}, "thread_name"};
  t.detach();
}

TEST(Thread, FunctionPointer) {
  Thread t1{&foo, "thread_name1"};
  Thread t2{foo, "thread_name2"};

  t1.join();
  t2.join();
}

TEST(Thread, Moveable) {
  auto done = std::make_unique<bool>(true);

  Thread t{[done = std::move(done)]() {
             EXPECT_TRUE(*done);
           },
           "thread_name"};

  t.join();
}

#endif

} // namespace
} // namespace eventuals::test
