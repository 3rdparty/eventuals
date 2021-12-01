#pragma once

#include "asio/ssl.hpp"
#include "eventuals/builder.h"
#include "eventuals/event-loop.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {

////////////////////////////////////////////////////////////////////////

enum class Protocol {
  IPV4,
  IPV6
};

////////////////////////////////////////////////////////////////////////

// Different ways a socket may be shutdown.
enum class ShutdownType {
  // Shutdown the send side of the socket.
  SEND = asio::ip::tcp::socket::shutdown_type::shutdown_send,

  // Shutdown the receive side of the socket.
  RECEIVE = asio::ip::tcp::socket::shutdown_type::shutdown_receive,

  // Shutdown both send and receive on the socket.
  BOTH = asio::ip::tcp::socket::shutdown_type::shutdown_both,
};

////////////////////////////////////////////////////////////////////////

namespace ssl {

////////////////////////////////////////////////////////////////////////

// Different methods supported by a context.
enum class SSLVersion {
  // Generic SSL version 2.
  SSLv2 = asio::ssl::context::sslv2,

  // SSL version 2 client.
  SSLv2_CLIENT = asio::ssl::context::sslv2_client,

  // SSL version 2 server.
  SSLv2_SERVER = asio::ssl::context::sslv2_server,

  // Generic SSL version 3.
  SSLv3 = asio::ssl::context::sslv3,

  // SSL version 3 client.
  SSLv3_CLIENT = asio::ssl::context::sslv3_client,

  // SSL version 3 server.
  SSLv3_SERVER = asio::ssl::context::sslv3_server,

  // Generic SSL/TLS.
  SSLv23 = asio::ssl::context::sslv23,

  // SSL/TLS client.
  SSLv23_CLIENT = asio::ssl::context::sslv23_client,

  // SSL/TLS server.
  SSLv23_SERVER = asio::ssl::context::sslv23_server,

  // Generic TLS.
  TLS = asio::ssl::context::tls,

  // TLS client.
  TLS_CLIENT = asio::ssl::context::tls_client,

  // TLS server.
  TLS_SERVER = asio::ssl::context::tls_server,

  // Generic TLS version 1.
  TLSv1 = asio::ssl::context::tlsv1,

  // TLS version 1 client.
  TLSv1_CLIENT = asio::ssl::context::tlsv1_client,

  // TLS version 1 server.
  TLSv1_SERVER = asio::ssl::context::tlsv1_server,

  // Generic TLS version 1.1.
  TLSv1_1 = asio::ssl::context::tlsv11,

  // TLS version 1.1 client.
  TLSv1_1_CLIENT = asio::ssl::context::tlsv11_client,

  // TLS version 1.1 server.
  TLSv1_1_SERVER = asio::ssl::context::tlsv11_server,

  // Generic TLS version 1.2.
  TLSv1_2 = asio::ssl::context::tlsv12,

  // TLS version 1.2 client.
  TLSv1_2_CLIENT = asio::ssl::context::tlsv12_client,

  // TLS version 1.2 server.
  TLSv1_2_SERVER = asio::ssl::context::tlsv12_server,

  // Generic TLS version 1.3.
  TLSv1_3 = asio::ssl::context::tlsv13,

  // TLS version 1.3 client.
  TLSv1_3_CLIENT = asio::ssl::context::tlsv13_client,

  // TLS version 1.3 server.
  TLSv1_3_SERVER = asio::ssl::context::tlsv13_server,
};

////////////////////////////////////////////////////////////////////////

enum class SSLVerifyMode {
  // No verification.
  NONE = asio::ssl::verify_none,

  // Verify the peer.
  PEER = asio::ssl::verify_peer,

  // Fail verification if the peer has no certificate.
  // Ignored unless PEER is set.
  FAIL_IF_NO_PEER_CERT = asio::ssl::verify_fail_if_no_peer_cert,

  // Do not request client certificate on renegotiation.
  // Ignored unless PEER is set.
  CLIENT_ONCE = asio::ssl::verify_client_once
};

// Bitmask type for peer verification.
using SSLVerifyModes = long;

////////////////////////////////////////////////////////////////////////

enum class SSLOption {
  // Implement various bug workarounds.
  DEFAULT_WORKAROUNDS = asio::ssl::context_base::default_workarounds,

  // Disable compression. Compression is disabled by default.
  NO_COMPRESSION = asio::ssl::context_base::no_compression,

  // Disable SSL v2.
  NO_SSLv2 = asio::ssl::context_base::no_sslv2,

  // Disable SSL v3.
  NO_SSLv3 = asio::ssl::context_base::no_sslv3,

  // Disable TLS v1.
  NO_TLSv1 = asio::ssl::context_base::no_tlsv1,

  // Disable TLS v1.1.
  NO_TLSv1_1 = asio::ssl::context_base::no_tlsv1_1,

  // Disable TLS v1.2.
  NO_TLSv1_2 = asio::ssl::context_base::no_tlsv1_2,

  // Disable TLS v1.3.
  NO_TLSv1_3 = asio::ssl::context_base::no_tlsv1_3,

  // Always create a new key when using tmp_dh parameters.
  SINGLE_DH_USE = asio::ssl::context_base::single_dh_use
};

// Bitmask type for SSL options.
using SSLOptions = long;

////////////////////////////////////////////////////////////////////////

// File format types.
enum class FileFormat {
  PEM,
  ASN1
};

////////////////////////////////////////////////////////////////////////

// Purpose of PEM password.
enum class PasswordPurpose {
  // The password is needed for reading/decryption.
  FOR_READING = asio::ssl::context::password_purpose::for_reading,

  // The password is needed for writing/encryption.
  FOR_WRITING = asio::ssl::context::password_purpose::for_writing
};

////////////////////////////////////////////////////////////////////////

// Different handshake types.
enum class HandshakeType {
  // Perform handshaking as a client.
  CLIENT = asio::ssl::stream<asio::ip::tcp::socket>::handshake_type::client,

  // Perform handshaking as a server.
  SERVER = asio::ssl::stream<asio::ip::tcp::socket>::handshake_type::server
};

////////////////////////////////////////////////////////////////////////

class SSLContext final {
 public:
  SSLContext(SSLVersion ssl_version);

  SSLContext(const SSLContext& that) = delete;
  SSLContext(SSLContext&& that) = default;

  SSLContext& operator=(const SSLContext& that) = delete;
  SSLContext& operator=(SSLContext&& that) = default;

  // Constructs a new ssl::SSLContext "builder" with the default
  // undefined values.
  static auto Builder();

 private:
  friend class Socket;

  // 20 bools. Yes.
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

  asio::ssl::context context_;

  asio::ssl::context& underlying_ssl_context_handle() {
    return context_;
  }
};

////////////////////////////////////////////////////////////////////////

template <
    bool has_method_ = false,
    bool has_certificate_authority_ = false,
    bool has_verify_path_ = false,
    bool has_verify_file_ = false,
    bool has_default_verify_paths_ = false,
    bool has_ssl_options_ = false,
    bool has_password_callback_ = false,
    bool has_verify_callback_ = false,
    bool has_verify_depth_ = false,
    bool has_verify_modes_ = false,
    bool has_certificate_ = false,
    bool has_certificate_file_ = false,
    bool has_certificate_chain_ = false,
    bool has_certificate_chain_file_ = false,
    bool has_private_key_ = false,
    bool has_private_key_file_ = false,
    bool has_rsa_private_key_ = false,
    bool has_rsa_private_key_file_ = false,
    bool has_tmp_dh_ = false,
    bool has_tmp_dh_file_ = false>
class SSLContext::_Builder final : public builder::Builder {
 public:
  ~_Builder() override = default;

 private:
  // Helper structures.
  struct ConstBuffer final {
    const char* data;
    size_t size;
  };

  struct ForLoadingFromFile final {
    std::string filename;
    FileFormat file_format;
  };

  struct ForLoadingFromMemory final {
    const char* data;
    size_t size;
    FileFormat file_format;
  };

  using ConstBuffers = std::vector<ConstBuffer>;
  using VerifyPaths = std::vector<std::string>;
  using VerifyFiles = std::vector<std::string>;

  using Certificate = ForLoadingFromMemory;
  using PrivateKey = ForLoadingFromMemory;
  using RSAPrivateKey = ForLoadingFromMemory;

  using CertificateFile = ForLoadingFromFile;
  using PrivateKeyFile = ForLoadingFromFile;
  using RSAPrivateKeyFile = ForLoadingFromFile;

 public:
  auto ssl_version(
      SSLVersion ssl_version) && {
    static_assert(!has_method_, "Duplicate 'ssl_version'");
    return Construct<_Builder>(
        ssl_version_.Set(ssl_version),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }

  // This function is used to add one trusted certification authority
  // from a memory buffer.
  auto add_certificate_authority(
      const char* source,
      size_t source_size) && {
    certificate_authorities_->emplace_back(source, source_size);
    return Construct<_Builder>(
        std::move(ssl_version_),
        certificate_authorities_.Set(
            std::move(certificate_authorities_).value()),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to specify the name of a directory containing
  // certification authority certificates.
  // Each file in the directory must contain a single certificate.
  // The files must be named using the subject name's hash
  // and an extension of ".0".
  auto add_verify_path(
      std::string&& path) && {
    verify_paths_->emplace_back(std::move(path));
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        verify_paths_.Set(std::move(verify_paths_).value()),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load the certificates for one or more trusted
  // certification authorities from a file.
  auto load_verify_file(
      std::string&& filename) && {
    verify_files_->emplace_back(std::move(filename));
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        verify_files_.Set(std::move(verify_files_).value()),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function specifies that the context should use the default,
  // system-dependent directories for locating certification
  // authority certificates.
  auto set_default_verify_paths() && {
    static_assert(
        !has_default_verify_paths_,
        "Duplicate 'set_default_verify_paths'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        default_verify_paths_.Set(true),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function may be used to configure the SSL options used by the context.
  auto set_options(
      SSLOptions ssl_options) && {
    static_assert(!has_ssl_options_, "Duplicate 'set_options'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        ssl_options_.Set(ssl_options),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to specify a callback function
  // to obtain password information about an encrypted key in PEM format.
  //
  // The function signature of the handler must be:
  //
  // std::string password_callback(
  //   std::size_t max_length,  // The maximum size for a password.
  //   password_purpose purpose // Whether password is for reading or writing.
  // );
  //
  // The return value of the callback is a string containing the password.
  template <typename PasswordCallback>
  auto set_password_callback(
      PasswordCallback&& callback) && {
    static_assert(!has_password_callback_, "Duplicate 'set_password_callback'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        password_callback_.Set(callback),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to specify a callback function
  // that will be called by the implementation when it
  // needs to verify a peer certificate.
  //
  // The function signature of the handler must be:
  //
  // bool verify_callback(
  //   bool preverified, // True if the certificate passed pre-verification.
  //   verify_context& ctx // The peer certificate and other context.
  // );
  //
  // The return value of the callback is true
  // if the certificate has passed verification, false otherwise.
  template <typename VerifyCallback>
  auto set_verify_callback(
      VerifyCallback&& callback) && {
    static_assert(!has_verify_callback_, "Duplicate 'set_verify_callback'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        verify_callback_.Set(callback),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function may be used to configure
  // the maximum verification depth allowed by the context.
  auto set_verify_depth(
      int depth) && {
    static_assert(!has_verify_depth_, "Duplicate 'set_verify_depth'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        verify_depth_.Set(depth),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function may be used to configure the peer verification mode used
  // by the context.
  auto set_verify_modes(
      SSLVerifyModes ssl_verify_mode) && {
    static_assert(!has_verify_modes_, "Duplicate 'set_verify_modes'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        verify_modes_.Set(ssl_verify_mode),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a certificate into the context from a buffer.
  auto use_certificate(
      const char* source,
      size_t source_size,
      FileFormat file_format) && {
    static_assert(!has_certificate_, "Duplicate 'use_certificate'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        certificate_.Set(source, source_size, file_format),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a certificate into the context from a file.
  auto use_certificate_file(
      std::string&& filename,
      FileFormat file_format) && {
    static_assert(!has_certificate_file_, "Duplicate 'use_certificate_file'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        certificate_file_.Set(std::move(filename), file_format),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a certificate chain into the context
  // from a buffer.
  auto use_certificate_chain(
      const char* source,
      size_t source_size) && {
    static_assert(!has_certificate_chain_, "Duplicate 'use_certificate_chain'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        certificate_chain_.Set(source, source_size),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a certificate chain into the context
  // from a file.
  // The file must use the PEM format.
  auto use_certificate_chain_file(
      std::string&& filename) && {
    static_assert(
        !has_certificate_chain_file_,
        "Duplicate 'use_certificate_chain_file'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        certificate_chain_file_.Set(std::move(filename)),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a private key into the context from a buffer.
  auto use_private_key(
      const char* source,
      size_t source_size,
      FileFormat file_format) && {
    static_assert(!has_private_key_, "Duplicate 'use_private_key'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        private_key_.Set(source, source_size, file_format),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a private key into the context from a file.
  auto use_private_key_file(
      std::string&& filename,
      FileFormat file_format) && {
    static_assert(!has_private_key_file_, "Duplicate 'use_private_key_file'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        private_key_file_.Set(std::move(filename), file_format),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load an RSA private key into the context
  // from a buffer.
  auto use_rsa_private_key(
      const char* source,
      size_t source_size,
      FileFormat file_format) && {
    static_assert(!has_rsa_private_key_, "Duplicate 'use_rsa_private_key'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        rsa_private_key_.Set(source, source_size, file_format),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load an RSA private key into the context
  // from a file.
  auto use_rsa_private_key_file(
      std::string&& filename,
      FileFormat file_format) && {
    static_assert(
        !has_rsa_private_key_file_,
        "Duplicate 'use_rsa_private_key_file'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        rsa_private_key_file_.Set(std::move(filename), file_format),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load Diffie-Hellman parameters into the context
  // from a buffer.
  // The buffer must use the PEM format.
  auto use_tmp_dh(
      const char* source,
      size_t source_size) && {
    static_assert(!has_tmp_dh_, "Duplicate 'use_tmp_dh'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        tmp_dh_.Set(source, source_size),
        std::move(tmp_dh_file_));
  }


  // This function is used to load Diffie-Hellman parameters into the context
  // from a file.
  // The file must use the PEM format.
  auto use_tmp_dh_file(
      std::string&& filename) && {
    static_assert(!has_tmp_dh_file_, "Duplicate 'use_tmp_dh_file'");
    return Construct<_Builder>(
        std::move(ssl_version_),
        std::move(certificate_authorities_),
        std::move(verify_paths_),
        std::move(verify_files_),
        std::move(default_verify_paths_),
        std::move(ssl_options_),
        std::move(password_callback_),
        std::move(verify_callback_),
        std::move(verify_depth_),
        std::move(verify_modes_),
        std::move(certificate_),
        std::move(certificate_file_),
        std::move(certificate_chain_),
        std::move(certificate_chain_file_),
        std::move(private_key_),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        tmp_dh_file_.Set(std::move(filename)));
  }


  SSLContext Build() && {
    static_assert(has_method_, "Missing 'ssl_version'");

    SSLContext ssl_context(ssl_version_.value());
    asio::error_code error;

    for (ConstBuffer& buffer : certificate_authorities_.value()) {
      ssl_context.underlying_ssl_context_handle().add_certificate_authority(
          asio::const_buffer(buffer.data, buffer.size),
          error);
      CHECK(!error) << "Could not add certificate authority due to error: "
                    << error.message();
    }

    for (std::string& path : verify_paths_.value()) {
      ssl_context.underlying_ssl_context_handle().add_verify_path(
          path,
          error);
      CHECK(!error) << "Could not add verify path due to error: "
                    << error.message();
    }

    for (std::string& path : verify_files_.value()) {
      ssl_context.underlying_ssl_context_handle().load_verify_file(
          path,
          error);
      CHECK(!error) << "Could not load verify file due to error: "
                    << error.message();
    }

    if constexpr (has_default_verify_paths_) {
      ssl_context.underlying_ssl_context_handle().set_default_verify_paths(
          error);
      CHECK(!error) << "Could not set default verify paths due to error: "
                    << error.message();
    }

    if constexpr (has_ssl_options_) {
      ssl_context.underlying_ssl_context_handle().set_options(
          ssl_options_.value(),
          error);
      CHECK(!error) << "Could not set SSL options due to error: "
                    << error.message();
    }

    if constexpr (has_password_callback_) {
      ssl_context.underlying_ssl_context_handle().set_password_callback(
          [password_callback = std::move(password_callback_.value())](
              std::size_t max_length,
              asio::ssl::context_base::password_purpose purpose) {
            return password_callback(
                max_length,
                static_cast<PasswordPurpose>(purpose));
          },
          error);
      CHECK(!error) << "Could not set password callback due to error: "
                    << error.message();
    }

    if constexpr (has_verify_callback_) {
      ssl_context.underlying_ssl_context_handle().set_verify_callback(
          [verify_callback = std::move(verify_callback_.value())](
              bool preverified,
              asio::ssl::verify_context& ctx) {
            return verify_callback(preverified, ctx);
          },
          error);
      CHECK(!error) << "Could not set verify callback due to error: "
                    << error.message();
    }

    if constexpr (has_verify_depth_) {
      ssl_context.underlying_ssl_context_handle().set_verify_depth(
          verify_depth_.value(),
          error);
      CHECK(!error) << "Could not set verify depth due to error: "
                    << error.message();
    }

    if constexpr (has_verify_modes_) {
      ssl_context.underlying_ssl_context_handle().set_verify_mode(
          verify_modes_.value(),
          error);
      CHECK(!error) << "Could not set verify mode due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_) {
      ssl_context.underlying_ssl_context_handle().use_certificate(
          asio::const_buffer(
              certificate_->data,
              certificate_->size),
          certificate_->file_format,
          error);
      CHECK(!error) << "Could not use certificate due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_file_) {
      ssl_context.underlying_ssl_context_handle().use_certificate_file(
          certificate_file_->filename,
          certificate_file_->file_format,
          error);
      CHECK(!error) << "Could not use certificate file due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_chain_) {
      ssl_context.underlying_ssl_context_handle().use_certificate_chain(
          asio::const_buffer(
              certificate_chain_->data,
              certificate_chain_->size),
          error);
      CHECK(!error) << "Could not use certificate chain due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_chain_file_) {
      ssl_context.underlying_ssl_context_handle().use_certificate_chain_file(
          certificate_chain_file_.value(),
          error);
      CHECK(!error) << "Could not use certificate chain file due to error: "
                    << error.message();
    }

    if constexpr (has_private_key_) {
      ssl_context.underlying_ssl_context_handle().use_private_key(
          asio::const_buffer(
              private_key_->data,
              private_key_->size),
          private_key_->file_format,
          error);
      CHECK(!error) << "Could not use private key due to error: "
                    << error.message();
    }

    if constexpr (has_private_key_file_) {
      ssl_context.underlying_ssl_context_handle().use_private_key_file(
          rsa_private_key_file_->filename,
          rsa_private_key_file_->file_format,
          error);
      CHECK(!error) << "Could not use private key file due to error: "
                    << error.message();
    }

    if constexpr (has_rsa_private_key_) {
      ssl_context.underlying_ssl_context_handle().use_rsa_private_key(
          asio::const_buffer(
              rsa_private_key_->data,
              rsa_private_key_->size),
          rsa_private_key_->file_format,
          error);
      CHECK(!error) << "Could not use RSA private key due to error: "
                    << error.message();
    }

    if constexpr (has_rsa_private_key_file_) {
      ssl_context.underlying_ssl_context_handle().use_rsa_private_key_file(
          rsa_private_key_file_->filename,
          rsa_private_key_file_->file_format,
          error);
      CHECK(!error) << "Could not use RSA private key file due to error: "
                    << error.message();
    }

    if constexpr (has_tmp_dh_) {
      ssl_context.underlying_ssl_context_handle().use_tmp_dh(
          asio::const_buffer(tmp_dh_->data, tmp_dh_->size),
          error);
      CHECK(!error) << "Could not use DH parameters due to error: "
                    << error.message();
    }

    if constexpr (has_tmp_dh_file_) {
      ssl_context.underlying_ssl_context_handle().use_tmp_dh_file(
          tmp_dh_file_.value(),
          error);
      CHECK(!error) << "Could not use DH parameters file due to error: "
                    << error.message();
    }

    return ssl_context;
  }

 private:
  friend class builder::Builder;
  friend class SSLContext;

  _Builder() = default;

  _Builder(
      builder::Field<SSLVersion, has_method_> ssl_version_)
    : ssl_version_(ssl_version_) {}

  builder::Field<
      SSLVersion,
      has_method_>
      ssl_version_;

  builder::RepeatedField<
      ConstBuffers,
      has_certificate_authority_>
      certificate_authorities_ = ConstBuffers{};

  builder::RepeatedField<
      VerifyPaths,
      has_verify_path_>
      verify_paths_ = VerifyPaths{};

  builder::RepeatedField<
      VerifyFiles,
      has_verify_file_>
      verify_files_ = VerifyFiles{};

  builder::Field<
      bool,
      has_default_verify_paths_>
      default_verify_paths_;

  builder::Field<
      SSLOptions,
      has_ssl_options_>
      ssl_options_;

  builder::Field<
      std::function<std::string(std::size_t, PasswordPurpose)>,
      has_password_callback_>
      password_callback_;

  builder::Field<
      std::function<bool(bool, asio::ssl::verify_context)>,
      has_verify_callback_>
      verify_callback_;

  builder::Field<
      int,
      has_verify_depth_>
      verify_depth_;

  builder::Field<
      SSLVerifyModes,
      has_verify_modes_>
      verify_modes_;

  builder::Field<
      Certificate,
      has_certificate_>
      certificate_;

  builder::Field<
      CertificateFile,
      has_certificate_file_>
      certificate_file_;

  builder::Field<
      ConstBuffer,
      has_certificate_chain_>
      certificate_chain_;

  builder::Field<
      std::string,
      has_certificate_chain_file_>
      certificate_chain_file_;

  builder::Field<
      PrivateKey,
      has_private_key_>
      private_key_;

  builder::Field<
      PrivateKeyFile,
      has_private_key_file_>
      private_key_file_;

  builder::Field<
      RSAPrivateKey,
      has_rsa_private_key_>
      rsa_private_key_;

  builder::Field<
      RSAPrivateKeyFile,
      has_rsa_private_key_file_>
      rsa_private_key_file_;

  builder::Field<
      ConstBuffer,
      has_tmp_dh_>
      tmp_dh_;

  builder::Field<
      std::string,
      has_tmp_dh_file_>
      tmp_dh_file_;
};

////////////////////////////////////////////////////////////////////////

inline auto SSLContext::Builder() {
  return SSLContext::_Builder();
}

////////////////////////////////////////////////////////////////////////

class Socket final {
 public:
  Socket(SSLContext& context, EventLoop& loop = EventLoop::Default())
    : loop_(loop),
      stream_(loop.io_context(), context.underlying_ssl_context_handle()) {}

  Socket(const Socket& that) = delete;
  Socket(Socket&& that) = delete;

  Socket& operator=(const Socket& that) = delete;
  Socket& operator=(Socket&& that) = delete;

  auto Open(Protocol protocol) {
    struct Data {
      Socket* socket;
      Protocol protocol;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, protocol})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          asio::post(
              data.socket->io_context(),
              [data,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                  return;
                } else {
                  if (data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is opened"));
                    return;
                  }

                  asio::error_code error;

                  switch (data.protocol) {
                    case Protocol::IPV4:
                      data.socket->underlying_socket_handle().open(
                          asio::ip::tcp::v4(),
                          error);
                      break;
                    case Protocol::IPV6:
                      data.socket->underlying_socket_handle().open(
                          asio::ip::tcp::v6(),
                          error);
                      break;
                    default:
                      k.Fail(std::runtime_error("Invalid protocol"));
                      return;
                  }

                  data.socket->protocol_.emplace(data.protocol);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  auto Bind(std::string&& ip, uint16_t port) {
    struct Data {
      Socket* socket;
      std::string ip;
      uint16_t port;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, std::move(ip), port})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          asio::post(
              data.socket->io_context(),
              [data = std::move(data),
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                  return;
                } else {
                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  if (data.socket->protocol_.has_value()) {
                    switch (data.socket->protocol_.value()) {
                      case Protocol::IPV4:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v4(data.ip, error),
                            data.port);
                        break;
                      case Protocol::IPV6:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v6(data.ip, error),
                            data.port);
                        break;
                    }
                  } else {
                    k.Fail(std::runtime_error("Protocol unspecified"));
                    return;
                  }

                  if (error) {
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  data.socket->underlying_socket_handle().bind(endpoint, error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  auto Connect(std::string&& ip, uint16_t port) {
    struct Data {
      Socket* socket;
      std::string ip;
      uint16_t port;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, std::move(ip), port})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  if (data.socket->protocol_.has_value()) {
                    switch (data.socket->protocol_.value()) {
                      case Protocol::IPV4:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v4(data.ip, error),
                            data.port);
                        break;
                      case Protocol::IPV6:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v6(data.ip, error),
                            data.port);
                        break;
                    }
                  } else {
                    data.completed = true;
                    k.Fail(std::runtime_error("Invalid protocol"));
                    return;
                  }

                  if (error) {
                    data.completed = true;
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  data.socket->underlying_socket_handle().async_connect(
                      endpoint,
                      [&](const asio::error_code& error) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start();
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Handshake(HandshakeType handshake_type) {}

  auto ReceiveSome(void* destination, size_t destination_size) {
    struct Data {
      Socket* socket;
      void* destination;
      size_t destination_size;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<size_t>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, destination, destination_size})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  if (data.destination_size == 0) {
                    data.completed = true;
                    k.Start(0);
                    return;
                  }

                  data.socket->underlying_stream_handle().async_read_some(
                      asio::buffer(data.destination, data.destination_size),
                      [&](const asio::error_code& error,
                          size_t bytes_transferred) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start(bytes_transferred);
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Receive(
      void* destination,
      size_t destination_size,
      size_t bytes_to_read) {
    struct Data {
      Socket* socket;
      void* destination;
      size_t destination_size;
      size_t bytes_to_read;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<size_t>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, destination, destination_size, bytes_to_read})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  // Do not allow to read more than destination_size.
                  data.bytes_to_read = std::min(
                      data.bytes_to_read,
                      data.destination_size);

                  if (data.bytes_to_read == 0) {
                    data.completed = true;
                    k.Start(0);
                    return;
                  }

                  // Start receiving.
                  // Will only succeed after the supplied buffer is full.
                  asio::async_read(
                      data.socket->underlying_stream_handle(),
                      asio::buffer(data.destination, data.bytes_to_read),
                      [&](const asio::error_code& error,
                          size_t bytes_transferred) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start(bytes_transferred);
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Send(const void* source, size_t source_size) {
    struct Data {
      Socket* socket;
      const void* source;
      size_t source_size;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<size_t>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, source, source_size})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  if (data.source_size == 0) {
                    data.completed = true;
                    k.Start(0);
                    return;
                  }

                  // Will only succeed after writing all of the data to socket.
                  asio::async_write(
                      data.socket->underlying_stream_handle(),
                      asio::buffer(data.source, data.source_size),
                      [&](const asio::error_code& error,
                          size_t bytes_transferred) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start(bytes_transferred);
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Shutdown(ShutdownType shutdown_type) {
    struct Data {
      Socket* socket;
      ShutdownType shutdown_type;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, shutdown_type})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          if (!data.socket->underlying_socket_handle().is_open()) {
            k.Fail(std::runtime_error("Socket is closed"));
            return;
          }

          asio::error_code error;

          data.socket->underlying_socket_handle().shutdown(
              static_cast<asio::socket_base::shutdown_type>(data.shutdown_type),
              error);

          if (!error) {
            k.Start();
          } else {
            k.Fail(std::runtime_error(error.message()));
          }
        });
  }

  auto Close() {
    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(this)
        .start([](Socket* socket, auto& k, Interrupt::Handler& handler) {
          asio::post(
              socket->io_context(),
              [socket,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (!socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;

                  socket->underlying_socket_handle().close(error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  bool IsOpen() {
    return underlying_socket_handle().is_open();
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  uint16_t BoundPort() {
    return underlying_socket_handle().local_endpoint().port();
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  std::string BoundIp() {
    return underlying_socket_handle().local_endpoint().address().to_string();
  }

 private:
  friend class Acceptor;

  EventLoop& loop_;
  asio::ssl::stream<asio::ip::tcp::socket> stream_;
  std::optional<Protocol> protocol_;

  asio::ssl::stream<asio::ip::tcp::socket>& underlying_stream_handle() {
    return stream_;
  }

  asio::ip::tcp::socket& underlying_socket_handle() {
    return stream_.next_layer();
  }

  asio::io_context& io_context() {
    return loop_.io_context();
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace ssl

////////////////////////////////////////////////////////////////////////

class Socket final {
 public:
  Socket(EventLoop& loop = EventLoop::Default())
    : loop_(loop),
      socket_(loop.io_context()) {}

  Socket(const Socket& that) = delete;
  Socket(Socket&& that) = delete;

  Socket& operator=(const Socket& that) = delete;
  Socket& operator=(Socket&& that) = delete;

  auto Open(Protocol protocol) {
    struct Data {
      Socket* socket;
      Protocol protocol;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, protocol})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          asio::post(
              data.socket->io_context(),
              [data,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is opened"));
                    return;
                  }

                  asio::error_code error;

                  switch (data.protocol) {
                    case Protocol::IPV4:
                      data.socket->underlying_socket_handle().open(
                          asio::ip::tcp::v4(),
                          error);
                      break;
                    case Protocol::IPV6:
                      data.socket->underlying_socket_handle().open(
                          asio::ip::tcp::v6(),
                          error);
                      break;
                    default:
                      k.Fail(std::runtime_error("Invalid protocol"));
                      return;
                  }

                  data.socket->protocol_.emplace(data.protocol);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  auto Bind(std::string&& ip, uint16_t port) {
    struct Data {
      Socket* socket;
      std::string ip;
      uint16_t port;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, std::move(ip), port})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          asio::post(
              data.socket->io_context(),
              [data = std::move(data),
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  if (data.socket->protocol_.has_value()) {
                    switch (data.socket->protocol_.value()) {
                      case Protocol::IPV4:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v4(data.ip, error),
                            data.port);
                        break;
                      case Protocol::IPV6:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v6(data.ip, error),
                            data.port);
                        break;
                    }
                  } else {
                    k.Fail(std::runtime_error("Protocol unspecified"));
                    return;
                  }

                  if (error) {
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  data.socket->underlying_socket_handle().bind(endpoint, error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  auto Connect(std::string&& ip, uint16_t port) {
    struct Data {
      Socket* socket;
      std::string ip;
      uint16_t port;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, std::move(ip), port})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  if (data.socket->protocol_.has_value()) {
                    switch (data.socket->protocol_.value()) {
                      case Protocol::IPV4:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v4(data.ip, error),
                            data.port);
                        break;
                      case Protocol::IPV6:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v6(data.ip, error),
                            data.port);
                        break;
                    }
                  } else {
                    data.completed = true;
                    k.Fail(std::runtime_error("Invalid protocol"));
                    return;
                  }

                  if (error) {
                    data.completed = true;
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  data.socket->underlying_socket_handle().async_connect(
                      endpoint,
                      [&](const asio::error_code& error) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start();
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto ReceiveSome(void* destination, size_t destination_size) {
    struct Data {
      Socket* socket;
      void* destination;
      size_t destination_size;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<size_t>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, destination, destination_size})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
                return;
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  if (data.destination_size == 0) {
                    data.completed = true;
                    k.Start(0);
                    return;
                  }

                  data.socket->underlying_socket_handle().async_read_some(
                      asio::buffer(data.destination, data.destination_size),
                      [&](const asio::error_code& error,
                          size_t bytes_transferred) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start(bytes_transferred);
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Receive(
      void* destination,
      size_t destination_size,
      size_t bytes_to_read) {
    struct Data {
      Socket* socket;
      void* destination;
      size_t destination_size;
      size_t bytes_to_read;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<size_t>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, destination, destination_size, bytes_to_read})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  // Do not allow to read more than destination_size.
                  data.bytes_to_read = std::min(
                      data.bytes_to_read,
                      data.destination_size);

                  if (data.bytes_to_read == 0) {
                    data.completed = true;
                    k.Start(0);
                    return;
                  }

                  // Start receiving.
                  // Will only succeed after the supplied buffer is full.
                  asio::async_read(
                      data.socket->underlying_socket_handle(),
                      asio::buffer(data.destination, data.bytes_to_read),
                      [&](const asio::error_code& error,
                          size_t bytes_transferred) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start(bytes_transferred);
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Send(const void* source, size_t source_size) {
    struct Data {
      Socket* socket;
      const void* source;
      size_t source_size;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<size_t>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, source, source_size})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.socket->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.socket->underlying_socket_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.socket->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  if (data.source_size == 0) {
                    data.completed = true;
                    k.Start(0);
                    return;
                  }

                  // Will only succeed after writing all of the data to socket.
                  asio::async_write(
                      data.socket->underlying_socket_handle(),
                      asio::buffer(data.source, data.source_size),
                      [&](const asio::error_code& error,
                          size_t bytes_transferred) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start(bytes_transferred);
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

  auto Shutdown(ShutdownType shutdown_type) {
    struct Data {
      Socket* socket;
      ShutdownType shutdown_type;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, shutdown_type})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          if (!data.socket->underlying_socket_handle().is_open()) {
            k.Fail(std::runtime_error("Socket is closed"));
            return;
          }

          asio::error_code error;

          data.socket->underlying_socket_handle().shutdown(
              static_cast<asio::socket_base::shutdown_type>(data.shutdown_type),
              error);

          if (!error) {
            k.Start();
          } else {
            k.Fail(std::runtime_error(error.message()));
          }
        });
  }

  auto Close() {
    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(this)
        .start([](Socket* socket, auto& k, Interrupt::Handler& handler) {
          asio::post(
              socket->io_context(),
              [socket,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (!socket->underlying_socket_handle().is_open()) {
                    k.Fail(std::runtime_error("Socket is closed"));
                    return;
                  }

                  asio::error_code error;

                  socket->underlying_socket_handle().close(error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  bool IsOpen() {
    return underlying_socket_handle().is_open();
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  uint16_t BoundPort() {
    return underlying_socket_handle().local_endpoint().port();
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  std::string BoundIp() {
    return underlying_socket_handle().local_endpoint().address().to_string();
  }

 private:
  friend class Acceptor;

  EventLoop& loop_;
  asio::ip::tcp::socket socket_;
  std::optional<Protocol> protocol_;

  asio::ip::tcp::socket& underlying_socket_handle() {
    return socket_;
  }

  asio::io_context& io_context() {
    return loop_.io_context();
  }
};

////////////////////////////////////////////////////////////////////////

class Acceptor final {
 public:
  Acceptor(EventLoop& loop = EventLoop::Default())
    : loop_(loop),
      acceptor_(loop.io_context()) {}

  Acceptor(const Acceptor& that) = delete;
  Acceptor(Acceptor&& that) = delete;

  Acceptor& operator=(const Acceptor& that) = delete;
  Acceptor& operator=(Acceptor&& that) = delete;

  auto Open(Protocol protocol) {
    struct Data {
      Acceptor* acceptor;
      Protocol protocol;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, protocol})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          asio::post(
              data.acceptor->io_context(),
              [data,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (data.acceptor->underlying_acceptor_handle().is_open()) {
                    k.Fail(std::runtime_error("Already opened"));
                    return;
                  }

                  asio::error_code error;

                  switch (data.protocol) {
                    case Protocol::IPV4:
                      data.acceptor->underlying_acceptor_handle().open(
                          asio::ip::tcp::v4(),
                          error);
                      break;
                    case Protocol::IPV6:
                      data.acceptor->underlying_acceptor_handle().open(
                          asio::ip::tcp::v6(),
                          error);
                      break;
                    default:
                      k.Fail(std::runtime_error("Invalid protocol"));
                      return;
                  }

                  data.acceptor->protocol_.emplace(data.protocol);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  auto Bind(std::string&& ip, uint16_t port) {
    struct Data {
      Acceptor* acceptor;
      std::string ip;
      uint16_t port;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, std::move(ip), port})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          asio::post(
              data.acceptor->io_context(),
              [data = std::move(data),
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (!data.acceptor->underlying_acceptor_handle().is_open()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  asio::error_code error;
                  asio::ip::tcp::endpoint endpoint;

                  if (data.acceptor->protocol_.has_value()) {
                    switch (data.acceptor->protocol_.value()) {
                      case Protocol::IPV4:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v4(data.ip, error),
                            data.port);
                        break;
                      case Protocol::IPV6:
                        endpoint = asio::ip::tcp::endpoint(
                            asio::ip::make_address_v6(data.ip, error),
                            data.port);
                        break;
                    }
                  } else {
                    k.Fail(std::runtime_error("Invalid protocol"));
                    return;
                  }

                  if (error) {
                    k.Fail(std::runtime_error(error.message()));
                    return;
                  }

                  data.acceptor->underlying_acceptor_handle()
                      .bind(endpoint, error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  auto Listen() {
    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(this)
        .start([](Acceptor* acceptor, auto& k, Interrupt::Handler& handler) {
          asio::post(
              acceptor->io_context(),
              [acceptor,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                } else {
                  if (!acceptor->underlying_acceptor_handle().is_open()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  asio::error_code error;

                  acceptor->underlying_acceptor_handle().listen(
                      asio::socket_base::max_listen_connections,
                      error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

 private:
  template <typename SocketType>
  auto AcceptImplementation(SocketType* socket) {
    struct Data {
      Acceptor* acceptor;
      SocketType* socket;

      // Used for interrupt handler due to
      // static_assert(sizeof(Handler<F>) <= SIZE) (callback.h(59,5))
      // requirement for handler.Install().
      void* k = nullptr;

      bool started = false;
      bool completed = false;
    };

    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(Data{this, socket})
        .start([](auto& data, auto& k, Interrupt::Handler& handler) {
          using K = std::decay_t<decltype(k)>;
          data.k = &k;

          handler.Install([&data]() {
            asio::post(data.acceptor->io_context(), [&]() {
              K& k = *static_cast<K*>(data.k);

              if (!data.started) {
                data.completed = true;
                k.Stop();
                return;
              } else if (!data.completed) {
                data.completed = true;
                asio::error_code error;
                data.acceptor->underlying_acceptor_handle().cancel(error);

                if (!error) {
                  k.Stop();
                } else {
                  k.Fail(std::runtime_error(error.message()));
                }
              }
            });
          });

          asio::post(
              data.acceptor->io_context(),
              [&]() {
                if (!data.completed) {
                  CHECK(!data.started);
                  data.started = true;

                  if (!data.acceptor->underlying_acceptor_handle().is_open()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  data.acceptor->underlying_acceptor_handle().async_accept(
                      data.socket->underlying_socket_handle(),
                      [&](const asio::error_code& error) {
                        if (!data.completed) {
                          data.completed = true;

                          if (!error) {
                            k.Start();
                          } else {
                            k.Fail(std::runtime_error(error.message()));
                          }
                        }
                      });
                }
              });
        });
  }

 public:
  auto Accept(Socket* socket) {
    return AcceptImplementation(socket);
  }

  auto Accept(ssl::Socket* socket) {
    return AcceptImplementation(socket);
  }

  auto Close() {
    return Eventual<void>()
        .interruptible()
        .raises<std::runtime_error>()
        .context(this)
        .start([](Acceptor* acceptor, auto& k, Interrupt::Handler& handler) {
          asio::post(
              acceptor->io_context(),
              [acceptor,
               &k,
               &handler]() {
                if (handler.interrupt().Triggered()) {
                  k.Stop();
                  return;
                } else {
                  if (!acceptor->underlying_acceptor_handle().is_open()) {
                    k.Fail(std::runtime_error("Acceptor is closed"));
                    return;
                  }

                  asio::error_code error;

                  acceptor->underlying_acceptor_handle().close(error);

                  if (!error) {
                    k.Start();
                  } else {
                    k.Fail(std::runtime_error(error.message()));
                  }
                }
              });
        });
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  bool IsOpen() {
    return underlying_acceptor_handle().is_open();
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  uint16_t BoundPort() {
    return underlying_acceptor_handle().local_endpoint().port();
  }

  // Maybe thread-unsafe.
  // If there is any other operation on acceptor
  // on a different thread, return value might be incorrect.
  // For test purposes only.
  std::string BoundIp() {
    return underlying_acceptor_handle().local_endpoint().address().to_string();
  }

 private:
  EventLoop& loop_;
  asio::ip::tcp::acceptor acceptor_;
  std::optional<Protocol> protocol_;

  asio::ip::tcp::acceptor& underlying_acceptor_handle() {
    return acceptor_;
  }

  asio::io_context& io_context() {
    return loop_.io_context();
  }
};

////////////////////////////////////////////////////////////////////////

} // namespace tcp
} // namespace ip
} // namespace eventuals
