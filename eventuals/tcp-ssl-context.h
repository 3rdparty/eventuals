#pragma once

#include "asio/ssl.hpp"
#include "glog/logging.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {
namespace ssl {

////////////////////////////////////////////////////////////////////////

// Different methods supported by a context.
enum class SSLVersion {
  // SSL version 2.
  SSLv2 = asio::ssl::context::sslv2,
  SSLv2_CLIENT = asio::ssl::context::sslv2_client,
  SSLv2_SERVER = asio::ssl::context::sslv2_server,

  // SSL version 3.
  SSLv3 = asio::ssl::context::sslv3,
  SSLv3_CLIENT = asio::ssl::context::sslv3_client,
  SSLv3_SERVER = asio::ssl::context::sslv3_server,

  // SSL/TLS.
  // NOTE: the comment above was taken from the ASIO reference.
  // It's probably wrong to assume that these methods support TLS.
  SSLv23 = asio::ssl::context::sslv23,
  SSLv23_CLIENT = asio::ssl::context::sslv23_client,
  SSLv23_SERVER = asio::ssl::context::sslv23_server,

  // TLS.
  TLS = asio::ssl::context::tls,
  TLS_CLIENT = asio::ssl::context::tls_client,
  TLS_SERVER = asio::ssl::context::tls_server,

  // TLS version 1.
  TLSv1 = asio::ssl::context::tlsv1,
  TLSv1_CLIENT = asio::ssl::context::tlsv1_client,
  TLSv1_SERVER = asio::ssl::context::tlsv1_server,

  // TLS version 1.1.
  TLSv1_1 = asio::ssl::context::tlsv11,
  TLSv1_1_CLIENT = asio::ssl::context::tlsv11_client,
  TLSv1_1_SERVER = asio::ssl::context::tlsv11_server,

  // TLS version 1.2.
  TLSv1_2 = asio::ssl::context::tlsv12,
  TLSv1_2_CLIENT = asio::ssl::context::tlsv12_client,
  TLSv1_2_SERVER = asio::ssl::context::tlsv12_server,

  // TLS version 1.3.
  TLSv1_3 = asio::ssl::context::tlsv13,
  TLSv1_3_CLIENT = asio::ssl::context::tlsv13_client,
  TLSv1_3_SERVER = asio::ssl::context::tlsv13_server,
};

////////////////////////////////////////////////////////////////////////

class SSLContext final {
 public:
  SSLContext(const SSLContext& that) = delete;
  SSLContext(SSLContext&& that) noexcept
    : context_(std::move(that.context_)),
      moved_out_(that.moved_out_) {
    that.moved_out_ = true;
  }

  SSLContext& operator=(const SSLContext& that) = delete;
  SSLContext& operator=(SSLContext&& that) noexcept {
    context_ = std::move(that.context_);
    moved_out_ = that.moved_out_;
    that.moved_out_ = true;

    return *this;
  }

  ~SSLContext() = default;

  // Constructs a new ssl::SSLContext "builder" with the default
  // undefined values.
  static auto Builder();

 private:
  explicit SSLContext(SSLVersion ssl_version)
    : context_(static_cast<asio::ssl::context::method>(ssl_version)) {}

  asio::ssl::context& ssl_context_handle() {
    CHECK(!moved_out_) << "Using already moved out SSLContext";
    return context_;
  }

  asio::ssl::context context_;
  bool moved_out_ = false;

  template <
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool,
      bool>
  class _Builder;

  friend class Socket;
};

////////////////////////////////////////////////////////////////////////

} // namespace ssl
} // namespace tcp
} // namespace ip
} // namespace eventuals

////////////////////////////////////////////////////////////////////////

#include "eventuals/tcp-ssl-context-builder.h"

////////////////////////////////////////////////////////////////////////
