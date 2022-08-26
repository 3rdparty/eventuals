#pragma once

#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "asio.hpp"
#include "asio/ssl.hpp"
#include "eventuals/http.hh"
#include "eventuals/x509.hh"
#include "gmock/gmock.h"

namespace eventuals::http::test {

// Provides an HTTP mock server. See existing tests for examples of
// how you can use 'EXPECT_CALL()' on methods like 'ReceivedHeaders()'
// in your tests to handle each accepted socket.
//
// Only one socket is accepted and handled at a time.
//
// NOTE: this class is only expected to be used in tests so it
// generously uses macros like 'EXPECT_*' and 'ADD_FAILURE'.
// TODO(alexmc): Move all method definitions into a separate .cc file.
class HttpMockServer final {
 public:
  // Abstracts whether or not we have a secure, i.e., TLS/SSL, socket
  // or an insecure socket.
  class Socket {
   public:
    virtual ~Socket() = default;
    virtual std::string Receive() = 0;
    virtual void Send(const std::string& data) = 0;
    virtual void Close() = 0;
  };

  HttpMockServer(const std::string& scheme);
  ~HttpMockServer();

  // Mock method that allows overloading in each test via an
  // 'EXPECT_CALL()' how to handle a newly accepted socket.
  MOCK_METHOD(
      void,
      Accepted,
      (std::unique_ptr<Socket>),
      (const));

  // Mock method that allows overloading in each test via an
  // 'EXPECT_CALL()' what to do with a socket after the headers have
  // been received.
  MOCK_METHOD(
      void,
      ReceivedHeaders,
      (std::unique_ptr<Socket>, const std::string&),
      (const));

  // TODO(benh): consider a 'ReceivedBody()' mock function but it's a
  // bit trickier since the body might be "chunked".

  // Returns an 'http::Client' that has been configured correctly for
  // this server.
  eventuals::http::Client Client();

  unsigned short port() const {
    return endpoint_.port();
  }

  std::string host() const {
    // NOTE: using "localhost" here to match the use of
    // 'asio::ip::address_v4::loopback()' as the endpoint IP address.
    return "localhost";
  }

  std::string authority() const {
    return host() + ":" + std::to_string(port());
  }

  std::string uri() const {
    return scheme_ + authority();
  }

  const std::optional<x509::Certificate>& certificate() const {
    return certificate_;
  }

 private:
  void Accept();

  std::string scheme_;
  asio::io_context io_context_;
  asio::ip::tcp::endpoint endpoint_;
  asio::ip::tcp::acceptor acceptor_;
  asio::ssl::context ssl_context_;
  std::optional<x509::Certificate> certificate_;
  std::thread thread_;
};

} // namespace eventuals::http::test
