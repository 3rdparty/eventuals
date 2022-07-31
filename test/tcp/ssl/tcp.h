#pragma once

#include "eventuals/expected.h"
#include "eventuals/rsa.h"
#include "eventuals/tcp-ssl.h"
#include "eventuals/x509.h"
#include "test/tcp/tcp.h"

namespace eventuals::test {

class TCPSSLTest : public TCPTest {
 public:
  eventuals::ip::tcp::ssl::SSLContext SetupSSLContextClient();
  eventuals::ip::tcp::ssl::SSLContext SetupSSLContextServer();

  std::string host() const {
    return "localhost";
  }

  const x509::Certificate& certificate();

 private:
  const rsa::Key& rsa_key();
  const std::string& pem_key();
  const std::string& pem_certificate();
};

} // namespace eventuals::test
