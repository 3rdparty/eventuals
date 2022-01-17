#pragma once

#include "asio.hpp"
#include "asio/ssl.hpp"
#include "eventuals/rsa.h"
#include "eventuals/x509.h"

////////////////////////////////////////////////////////////////////////

// Provides an HTTP mock server. See existing tests for examples of
// how you can use 'EXPECT_CALL()' on methods like 'ReceivedHeaders()'
// in your tests to handle each accepted socket.
//
// Only one socket is accepted and handled at a time.
//
// NOTE: this class is only expected to be used in tests so it
// generously uses macros like 'EXPECT_*' and 'ADD_FAILURE'.
class HttpMockServer {
 public:
  // Abstracts whether or not we have a secure, i.e., TLS/SSL, socket
  // or an insecure socket.
  class Socket {
   public:
    virtual ~Socket() {}
    virtual std::string Receive() = 0;
    virtual void Send(const std::string& data) = 0;
    virtual void Close() = 0;

   protected:
    constexpr static size_t kBufferSize = 4096;
  };

  // Implementation of an insecure socket, i.e, no TLS/SSL.
  class InsecureSocket : public Socket {
   public:
    InsecureSocket(asio::ip::tcp::socket socket)
      : socket_(std::move(socket)) {}

    std::string Receive() override {
      asio::error_code error;
      char data[kBufferSize];
      size_t bytes = socket_.receive(
          asio::buffer(data, kBufferSize),
          /* flags = */ 0,
          error);
      if (error) {
        ADD_FAILURE() << "Failed to receive: " << error.message();
        return std::string();
      } else {
        return std::string(data, bytes);
      }
    }

    void Send(const std::string& data) override {
      size_t bytes = socket_.send(asio::buffer(data.data(), data.size()));
      EXPECT_EQ(bytes, data.size());
    }

    void Close() override {
      asio::error_code error;
      socket_.close(error);
      if (error) {
        ADD_FAILURE() << "Failed to close the socket: " << error.message();
      }
    }

   private:
    asio::ip::tcp::socket socket_;
  };

  // Implementation of an secure socket.
  class SecureSocket : public Socket {
   public:
    SecureSocket(asio::ssl::stream<asio::ip::tcp::socket> stream)
      : stream_(std::move(stream)) {}

    std::string Receive() override {
      asio::error_code error;
      char data[kBufferSize];
      size_t bytes = stream_.read_some(asio::buffer(data, kBufferSize), error);
      if (error) {
        ADD_FAILURE() << "Failed to receive: " << error.message();
        return std::string();
      } else {
        return std::string(data, bytes);
      }
    }

    void Send(const std::string& data) override {
      size_t bytes = stream_.write_some(asio::buffer(data.data(), data.size()));
      EXPECT_EQ(bytes, data.size());
    }

    void Close() override {
      asio::error_code error;
      stream_.next_layer().close(error);
      if (error) {
        ADD_FAILURE() << "Failed to close the socket: " << error.message();
      }
    }

   private:
    asio::ssl::stream<asio::ip::tcp::socket> stream_;
  };

  HttpMockServer(const std::string& scheme)
    : scheme_(scheme),
      acceptor_(io_context_),
      ssl_context_(asio::ssl::context::sslv23) {
    // Ensure we can first open, bind, listen an "acceptor".
    asio::error_code error;

    acceptor_.open(asio::ip::tcp::v4(), error);

    EXPECT_FALSE(error) << error.message();

    // NOTE: using 'loopback()' here to match with hostname
    // "localhost" in 'host()' below.
    acceptor_.bind(
        asio::ip::tcp::endpoint(
            asio::ip::address_v4::loopback(),
            0),
        error);

    EXPECT_FALSE(error) << error.message();

    acceptor_.listen(asio::socket_base::max_listen_connections, error);

    EXPECT_FALSE(error) << error.message();

    endpoint_ = acceptor_.local_endpoint();

    EXPECT_FALSE(error) << error.message();

    // Now configure our SSL context with a newly generated X509
    // certificate that we self-sign with a newly generated RSA
    // private key.
    auto key = rsa::Key::Builder().Build();

    CHECK(key) << "Failed to generate RSA private key";

    auto pem_key = pem::Encode(*key);

    CHECK(pem_key) << "Failed to PEM encode RSA private key";

    ssl_context_.use_private_key(
        asio::buffer(*pem_key),
        asio::ssl::context::pem);

    auto certificate = x509::Certificate::Builder()
                           .subject_key(rsa::Key(*key))
                           .sign_key(rsa::Key(*key))
                           .hostname(host())
                           .Build();

    CHECK(certificate) << "Failed to generate X509 certificate";

    certificate_ = *certificate;

    auto pem_certificate = pem::Encode(*certificate);

    CHECK(pem_certificate) << "Failed to PEM encode X509 certificate";

    ssl_context_.use_certificate_chain(
        asio::buffer(*pem_certificate));

    // Now set up what our mock functions will do by default if the
    // test using this class doesn't do 'EXPECT_CALL()'.
    //
    // All of these callbacks are made by a new thread that we create
    // hence the use of blocking functions (and only one socket is
    // accepted and handled at a time).
    //
    // NOTE: using 'EXPECT_CALL().WillRepeatedly()' here instead of
    // 'ON_CALL().WillByDefault()' to suppress "Uninteresting mock
    // function" messages as suggested by the gmock cookbook.
    EXPECT_CALL(*this, Accepted)
        .WillRepeatedly(
            [this](std::unique_ptr<Socket> socket) {
              // Receive data up to end of headers.
              std::string data;

              do {
                std::string buffer = socket->Receive();
                if (buffer.empty()) {
                  socket->Close();
                  return;
                }
                data += buffer;
              } while (data.find("\r\n\r\n") == std::string::npos);

              ReceivedHeaders(std::move(socket), data);
            });

    // NOTE: using 'EXPECT_CALL().WillRepeatedly()' here instead of
    // 'ON_CALL().WillByDefault()' to suppress "Uninteresting mock
    // function" messages as suggested by the gmock cookbook.
    EXPECT_CALL(*this, ReceivedHeaders)
        .WillRepeatedly(
            [](std::unique_ptr<Socket> socket, const std::string& data) {
              socket->Close();
            });

    // Now create the thread for accepting and handlings sockets,
    // which also appropriately handles whether or not to expect
    // secure ('https://') or insecure ('http://') clients.
    thread_ = std::thread([this]() {
      do {
        // NOTE: using 'async_accept()' instead of just 'accept()' so
        // that we can reliably interrupt the acceptor in
        // '~HttpMockServer()' across different operating systems.
        acceptor_.async_accept(
            [this](asio::error_code error, asio::ip::tcp::socket socket) {
              if (!error) {
                if (scheme_ == "http://") {
                  Accepted(std::unique_ptr<Socket>(
                      new InsecureSocket(std::move(socket))));
                } else {
                  CHECK_EQ(scheme_, "https://");

                  asio::ssl::stream<asio::ip::tcp::socket> stream(
                      std::move(socket),
                      ssl_context_);

                  stream.set_verify_mode(asio::ssl::verify_none);
                  stream.handshake(asio::ssl::stream_base::server, error);

                  if (error) {
                    ADD_FAILURE()
                        << "Failed to perform TLS/SSL handshake: "
                        << error.message();
                  } else {
                    Accepted(std::unique_ptr<Socket>(
                        new SecureSocket(std::move(stream))));
                  }
                }
              }
            });

        // Continuously accept unless the acceptor has been cancelled
        // or the 'io_context' was stopped.
        if (run_.load() && io_context_.run_one()) {
          // Need to do 'restart()' in order to call 'run_one()'
          // again. While this call "races" with doing 'stop()' in the
          // destructor because 'run_.store(false)' happens-before the
          // call to 'stop()' we will not call 'run_one()' even if
          // this call to 'restart()' overrides the call to 'stop()'.
          io_context_.restart();
          continue;
        } else {
          // Must be that 'io_context' has been stopped!
          break;
        }
      } while (true);
    });
  }

  ~HttpMockServer() {
    // Signal that we should not keep running. Must be done before we
    // do 'stop()' on the 'io_context'.
    run_.store(false);

    // Need to do a 'cancel()' on the 'acceptor' in addition to
    // 'stop()' on the 'io_context' to ensure 'accept()' returns and
    // the thread will exit and be joinable.
    acceptor_.cancel();

    io_context_.stop();

    asio::error_code error;
    acceptor_.close(error);
    EXPECT_FALSE(error) << error.message();

    thread_.join();
  }

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
  eventuals::http::Client Client() {
    if (scheme_ == "https://") {
      CHECK(certificate_);
      return eventuals::http::Client::Builder()
          .certificate(x509::Certificate(*certificate_))
          .Build();
    } else {
      return eventuals::http::Client::Builder()
          .Build();
    }
  }

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

  const auto& certificate() const {
    return certificate_;
  }

 private:
  std::string scheme_;
  asio::io_context io_context_;
  asio::ip::tcp::endpoint endpoint_;
  asio::ip::tcp::acceptor acceptor_;
  asio::ssl::context ssl_context_;
  std::optional<x509::Certificate> certificate_;
  std::thread thread_;
  std::atomic<bool> run_ = true;
};

////////////////////////////////////////////////////////////////////////
