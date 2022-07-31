#pragma once

#include "eventuals/builder.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace ip {
namespace tcp {
namespace ssl {

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
  PEM = asio::ssl::context_base::file_format::pem,
  ASN1 = asio::ssl::context_base::file_format::asn1
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

// NOTE: we can't specify the structures inside private section of
// the builder, because they will inherit template arguments,
// which will produce compilation errors.
namespace helpers {

////////////////////////////////////////////////////////////////////////

struct ConstBuffer final {
  const char* data;
  size_t size;
};

struct LoadFromFile final {
  std::string filename;
  FileFormat file_format;
};

struct LoadFromMemory final {
  const char* data;
  size_t size;
  FileFormat file_format;
};

using ConstBuffers = std::vector<ConstBuffer>;
using VerifyPaths = std::vector<std::string>;
using VerifyFiles = std::vector<std::string>;

using Certificate = LoadFromMemory;
using PrivateKey = LoadFromMemory;
using RSAPrivateKey = LoadFromMemory;

using CertificateFile = LoadFromFile;
using PrivateKeyFile = LoadFromFile;
using RSAPrivateKeyFile = LoadFromFile;

////////////////////////////////////////////////////////////////////////

} // namespace helpers

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
  auto certificate_authority(
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
  auto verify_path(
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
  auto verify_file(
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
  auto default_verify_paths() && {
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


  // This function may be used to
  // configure the SSL options used by the context.
  auto options(
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
  auto password_callback(
      PasswordCallback&& callback) && {
    static_assert(
        !has_password_callback_,
        "Duplicate 'set_password_callback'");
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
  auto verify_callback(
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
  auto verify_depth(
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
  auto verify_modes(
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


  // This function is used to load
  // a certificate into the context from a buffer.
  auto certificate(
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
        certificate_.Set(
            source,
            source_size,
            file_format),
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
  auto certificate_file(
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
        certificate_file_.Set(
            std::move(filename),
            file_format),
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
  auto certificate_chain(
      const char* source,
      size_t source_size) && {
    static_assert(
        !has_certificate_chain_,
        "Duplicate 'use_certificate_chain'");
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
  auto certificate_chain_file(
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


  // This function is used to load a
  // private key into the context from a buffer.
  auto private_key(
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
        private_key_.Set(
            source,
            source_size,
            file_format),
        std::move(private_key_file_),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load a private key into the context from a file.
  auto private_key_file(
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
        private_key_file_.Set(
            std::move(filename),
            file_format),
        std::move(rsa_private_key_),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load an RSA private key into the context
  // from a buffer.
  auto rsa_private_key(
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
        rsa_private_key_.Set(
            source,
            source_size,
            file_format),
        std::move(rsa_private_key_file_),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load an RSA private key into the context
  // from a file.
  auto rsa_private_key_file(
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
        rsa_private_key_file_.Set(
            std::move(filename),
            file_format),
        std::move(tmp_dh_),
        std::move(tmp_dh_file_));
  }


  // This function is used to load Diffie-Hellman parameters into the context
  // from a buffer.
  // The buffer must use the PEM format.
  auto tmp_dh(
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
  auto tmp_dh_file(
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

    for (auto& buffer : certificate_authorities_.value()) {
      ssl_context.ssl_context_handle().add_certificate_authority(
          asio::const_buffer(buffer.data, buffer.size),
          error);
      CHECK(!error) << "Could not add certificate authority due to error: "
                    << error.message();
    }

    for (std::string& path : verify_paths_.value()) {
      ssl_context.ssl_context_handle().add_verify_path(
          path,
          error);
      CHECK(!error) << "Could not add verify path due to error: "
                    << error.message();
    }

    for (std::string& path : verify_files_.value()) {
      ssl_context.ssl_context_handle().load_verify_file(
          path,
          error);
      CHECK(!error) << "Could not load verify file due to error: "
                    << error.message();
    }

    if constexpr (has_default_verify_paths_) {
      ssl_context.ssl_context_handle().set_default_verify_paths(
          error);
      CHECK(!error) << "Could not set default verify paths due to error: "
                    << error.message();
    }

    if constexpr (has_ssl_options_) {
      ssl_context.ssl_context_handle().set_options(
          ssl_options_.value(),
          error);
      CHECK(!error) << "Could not set SSL options due to error: "
                    << error.message();
    }

    if constexpr (has_password_callback_) {
      ssl_context.ssl_context_handle().set_password_callback(
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
      ssl_context.ssl_context_handle().set_verify_callback(
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
      ssl_context.ssl_context_handle().set_verify_depth(
          verify_depth_.value(),
          error);
      CHECK(!error) << "Could not set verify depth due to error: "
                    << error.message();
    }

    if constexpr (has_verify_modes_) {
      ssl_context.ssl_context_handle().set_verify_mode(
          verify_modes_.value(),
          error);
      CHECK(!error) << "Could not set verify mode due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_) {
      ssl_context.ssl_context_handle().use_certificate(
          asio::const_buffer(
              certificate_->data,
              certificate_->size),
          static_cast<
              asio::ssl::context_base::file_format>(
              certificate_->file_format),
          error);
      CHECK(!error) << "Could not use certificate due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_file_) {
      ssl_context.ssl_context_handle().use_certificate_file(
          certificate_file_->filename,
          static_cast<
              asio::ssl::context_base::file_format>(
              certificate_file_->file_format),
          error);
      CHECK(!error) << "Could not use certificate file due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_chain_) {
      ssl_context.ssl_context_handle().use_certificate_chain(
          asio::const_buffer(
              certificate_chain_->data,
              certificate_chain_->size),
          error);
      CHECK(!error) << "Could not use certificate chain due to error: "
                    << error.message();
    }

    if constexpr (has_certificate_chain_file_) {
      ssl_context.ssl_context_handle().use_certificate_chain_file(
          certificate_chain_file_.value(),
          error);
      CHECK(!error) << "Could not use certificate chain file due to error: "
                    << error.message();
    }

    if constexpr (has_private_key_) {
      ssl_context.ssl_context_handle().use_private_key(
          asio::const_buffer(
              private_key_->data,
              private_key_->size),
          static_cast<
              asio::ssl::context_base::file_format>(
              private_key_->file_format),
          error);
      CHECK(!error) << "Could not use private key due to error: "
                    << error.message();
    }

    if constexpr (has_private_key_file_) {
      ssl_context.ssl_context_handle().use_private_key_file(
          rsa_private_key_file_->filename,
          static_cast<
              asio::ssl::context_base::file_format>(
              rsa_private_key_file_->file_format),
          error);
      CHECK(!error) << "Could not use private key file due to error: "
                    << error.message();
    }

    if constexpr (has_rsa_private_key_) {
      ssl_context.ssl_context_handle().use_rsa_private_key(
          asio::const_buffer(
              rsa_private_key_->data,
              rsa_private_key_->size),
          static_cast<
              asio::ssl::context_base::file_format>(
              rsa_private_key_->file_format),
          error);
      CHECK(!error) << "Could not use RSA private key due to error: "
                    << error.message();
    }

    if constexpr (has_rsa_private_key_file_) {
      ssl_context.ssl_context_handle().use_rsa_private_key_file(
          rsa_private_key_file_->filename,
          static_cast<
              asio::ssl::context_base::file_format>(
              rsa_private_key_file_->file_format),
          error);
      CHECK(!error) << "Could not use RSA private key file due to error: "
                    << error.message();
    }

    if constexpr (has_tmp_dh_) {
      ssl_context.ssl_context_handle().use_tmp_dh(
          asio::const_buffer(tmp_dh_->data, tmp_dh_->size),
          error);
      CHECK(!error) << "Could not use DH parameters due to error: "
                    << error.message();
    }

    if constexpr (has_tmp_dh_file_) {
      ssl_context.ssl_context_handle().use_tmp_dh_file(
          tmp_dh_file_.value(),
          error);
      CHECK(!error) << "Could not use DH parameters file due to error: "
                    << error.message();
    }

    return ssl_context;
  }

 private:
  _Builder() = default;

  _Builder(
      builder::Field<
          SSLVersion,
          has_method_> ssl_version,
      builder::RepeatedField<
          helpers::ConstBuffers,
          has_certificate_authority_> certificate_authorities,
      builder::RepeatedField<
          helpers::VerifyPaths,
          has_verify_path_> verify_paths,
      builder::RepeatedField<
          helpers::VerifyFiles,
          has_verify_file_> verify_files,
      builder::Field<
          bool,
          has_default_verify_paths_> default_verify_paths,
      builder::Field<
          SSLOptions,
          has_ssl_options_> ssl_options,
      builder::Field<
          std::function<std::string(std::size_t, PasswordPurpose)>,
          has_password_callback_> password_callback,
      builder::Field<
          std::function<bool(bool, asio::ssl::verify_context)>,
          has_verify_callback_> verify_callback,
      builder::Field<
          int,
          has_verify_depth_> verify_depth,
      builder::Field<
          SSLVerifyModes,
          has_verify_modes_> verify_modes,
      builder::Field<
          helpers::Certificate,
          has_certificate_> certificate,
      builder::Field<
          helpers::CertificateFile,
          has_certificate_file_> certificate_file,
      builder::Field<
          helpers::ConstBuffer,
          has_certificate_chain_> certificate_chain,
      builder::Field<
          std::string,
          has_certificate_chain_file_> certificate_chain_file,
      builder::Field<
          helpers::PrivateKey,
          has_private_key_> private_key,
      builder::Field<
          helpers::PrivateKeyFile,
          has_private_key_file_> private_key_file,
      builder::Field<
          helpers::RSAPrivateKey,
          has_rsa_private_key_> rsa_private_key,
      builder::Field<
          helpers::RSAPrivateKeyFile,
          has_rsa_private_key_file_> rsa_private_key_file,
      builder::Field<
          helpers::ConstBuffer,
          has_tmp_dh_> tmp_dh,
      builder::Field<
          std::string,
          has_tmp_dh_file_> tmp_dh_file)
    : ssl_version_(std::move(ssl_version)),
      certificate_authorities_(std::move(certificate_authorities)),
      verify_paths_(std::move(verify_paths)),
      verify_files_(std::move(verify_files)),
      default_verify_paths_(std::move(default_verify_paths)),
      ssl_options_(std::move(ssl_options)),
      password_callback_(std::move(password_callback)),
      verify_callback_(std::move(verify_callback)),
      verify_depth_(std::move(verify_depth)),
      verify_modes_(std::move(verify_modes)),
      certificate_(std::move(certificate)),
      certificate_file_(std::move(certificate_file)),
      certificate_chain_(std::move(certificate_chain)),
      certificate_chain_file_(std::move(certificate_chain_file)),
      private_key_(std::move(private_key)),
      private_key_file_(std::move(private_key_file)),
      rsa_private_key_(std::move(rsa_private_key)),
      rsa_private_key_file_(std::move(rsa_private_key_file)),
      tmp_dh_(std::move(tmp_dh)),
      tmp_dh_file_(std::move(tmp_dh_file)) {}

  builder::Field<
      SSLVersion,
      has_method_>
      ssl_version_;

  builder::RepeatedField<
      helpers::ConstBuffers,
      has_certificate_authority_>
      certificate_authorities_ = helpers::ConstBuffers{};

  builder::RepeatedField<
      helpers::VerifyPaths,
      has_verify_path_>
      verify_paths_ = helpers::VerifyPaths{};

  builder::RepeatedField<
      helpers::VerifyFiles,
      has_verify_file_>
      verify_files_ = helpers::VerifyFiles{};

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
      helpers::Certificate,
      has_certificate_>
      certificate_;

  builder::Field<
      helpers::CertificateFile,
      has_certificate_file_>
      certificate_file_;

  builder::Field<
      helpers::ConstBuffer,
      has_certificate_chain_>
      certificate_chain_;

  builder::Field<
      std::string,
      has_certificate_chain_file_>
      certificate_chain_file_;

  builder::Field<
      helpers::PrivateKey,
      has_private_key_>
      private_key_;

  builder::Field<
      helpers::PrivateKeyFile,
      has_private_key_file_>
      private_key_file_;

  builder::Field<
      helpers::RSAPrivateKey,
      has_rsa_private_key_>
      rsa_private_key_;

  builder::Field<
      helpers::RSAPrivateKeyFile,
      has_rsa_private_key_file_>
      rsa_private_key_file_;

  builder::Field<
      helpers::ConstBuffer,
      has_tmp_dh_>
      tmp_dh_;

  builder::Field<
      std::string,
      has_tmp_dh_file_>
      tmp_dh_file_;

  friend class builder::Builder;
  friend class SSLContext;
};

////////////////////////////////////////////////////////////////////////

inline auto SSLContext::Builder() {
  return SSLContext::_Builder<
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false>();
}

////////////////////////////////////////////////////////////////////////

} // namespace ssl
} // namespace tcp
} // namespace ip
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
