#pragma once

#include "test/tcp/tcp.h"

namespace eventuals::test {

class TCPSSLTest : public TCPTest {
 public:
  static void SetupSSLContext(eventuals::ip::tcp::ssl::SSLContext& context);
};

} // namespace eventuals::test