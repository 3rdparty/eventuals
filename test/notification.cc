#include "eventuals/notification.h"

#include <future>

#include "eventuals/do-all.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {
namespace {

TEST(NotificationTest, NotifyThenWait) {
  Notification notification;

  *(notification.Notify()
    >> notification.WaitForNotification());
}

TEST(NotificationTest, WaitThenNotify) {
  Notification notification;

  auto [future, wait] = PromisifyForTest(notification.WaitForNotification());

  wait.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  *notification.Notify();

  future.wait();
}


TEST(NotificationTest, MultipleWaits) {
  Notification notification;

  auto [future, waits] = PromisifyForTest(
      DoAll(
          notification.WaitForNotification(),
          notification.WaitForNotification(),
          notification.WaitForNotification()));

  waits.Start();

  EXPECT_EQ(
      std::future_status::timeout,
      future.wait_for(std::chrono::seconds(0)));

  *notification.Notify();

  future.wait();
}

} // namespace
} // namespace eventuals::test
