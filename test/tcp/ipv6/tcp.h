#pragma once

#include "test/tcp/tcp.h"

namespace eventuals::test {

class TCPIPV6Test : public TCPTest {
 public:
  inline static const char* kLocalHostIPV6 = "::1";
  inline static const char* kAnyIPV6 = "::";
};

} // namespace eventuals::test
