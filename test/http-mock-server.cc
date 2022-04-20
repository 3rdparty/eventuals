#include "test/http-mock-server.h"

#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "asio.hpp"
#include "asio/ssl.hpp"
#include "eventuals/http.h"
#include "eventuals/rsa.h"
#include "eventuals/x509.h"
#include "gtest/gtest.h"

namespace eventuals::http::test {
namespace {

constexpr static size_t kBufferSize = 4096;

// Implementation of an insecure socket, i.e, no TLS/SSL.
class InsecureSocket final : public HttpMockServer::Socket {
 public:
  InsecureSocket(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {}

  ~InsecureSocket() override = default;

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
class SecureSocket final : public HttpMockServer::Socket {
 public:
  SecureSocket(asio::ssl::stream<asio::ip::tcp::socket> stream)
    : stream_(std::move(stream)) {}

  ~SecureSocket() override = default;

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

} // namespace

HttpMockServer::HttpMockServer(const std::string& scheme)
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
  // NOTE: We are using static variables to prevent regeneration
  // of keys and certificates on every constructor call.
  static auto key = rsa::Key::Builder().Build();

  CHECK(key) << "Failed to generate RSA private key";

  static auto pem_key = pem::Encode(*key);

  CHECK(pem_key) << "Failed to PEM encode RSA private key";

  ssl_context_.use_private_key(
      asio::buffer(*pem_key),
      asio::ssl::context::pem);

  static auto certificate = x509::Certificate::Builder()
                                .subject_key(rsa::Key(*key))
                                .sign_key(rsa::Key(*key))
                                .hostname(host())
                                .Build();

  CHECK(certificate) << "Failed to generate X509 certificate";

  certificate_ = *certificate;

  static auto pem_certificate = pem::Encode(*certificate);

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
    // Start the infinite "accept loop".
    Accept();

    // Run "forever" until the destructor stops us.
    //
    // NOTE: this should never return because 'Accept()' will
    // recursively invoke itself after handling a connection.
    io_context_.run();
  });
}

HttpMockServer::~HttpMockServer() {
  // Need to do a 'cancel()' on the 'acceptor' in addition to
  // 'stop()' on the 'io_context' to ensure 'accept()' returns and
  // the thread will exit and be joinable.
  acceptor_.cancel();

  io_context_.stop();

  thread_.join();

  asio::error_code error;
  acceptor_.close(error);
  EXPECT_FALSE(error) << error.message();
}

eventuals::http::Client HttpMockServer::Client() {
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

void HttpMockServer::Accept() {
  // NOTE: using 'async_accept()' instead of just 'accept()' so
  // that we can reliably interrupt the acceptor in
  // '~HttpMockServer()' across different operating systems by
  // just calling 'stop()' on the 'io_context'.
  acceptor_.async_accept(
      [this](asio::error_code error, asio::ip::tcp::socket socket) {
        if (!error) {
          if (scheme_ == "http://") {
            Accepted(std::unique_ptr<Socket>(
                new InsecureSocket(std::move(socket))));

            Accept(); // Accept the next connection!
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

              Accept(); // Accept the next connection!
            }
          }
        }
      });
}

} // namespace eventuals::http::test
