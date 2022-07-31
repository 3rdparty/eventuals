#pragma once

#include "eventuals/tcp.h"
#include "eventuals/then.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/event-loop-test.h"
#include "test/promisify-for-test.h"

namespace eventuals::test {

class TCPTest : public eventuals::test::EventLoopTest {
 public:
  inline static const char* kLocalHostIPV4 = "127.0.0.1";
  inline static const char* kAnyIPV4 = "0.0.0.0";
  inline static const int kAnyPort = 0;

  inline static const char* kTestData = "Hello Eventuals!";
  // +1 for null terminator.
  inline static const size_t kTestDataSize = strlen(kTestData) + 1;
};

} // namespace eventuals::test
