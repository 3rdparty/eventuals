#pragma once

#include <filesystem>

#include "eventuals/expected.h"
#include "eventuals/rsa.h"
#include "openssl/bio.h"
#include "openssl/pem.h"
#include "openssl/x509.h"
#include "openssl/x509v3.h"

////////////////////////////////////////////////////////////////////////

namespace x509 {

////////////////////////////////////////////////////////////////////////

// Forward declaration.
class CertificateBuilder;

////////////////////////////////////////////////////////////////////////

class Certificate {
 public:
  static CertificateBuilder Builder();

  Certificate(std::unique_ptr<X509, decltype(&X509_free)> certificate)
    : certificate_(std::move(certificate)) {}

  Certificate(const Certificate& that)
    : Certificate(Copy(that)) {}

  Certificate(Certificate&& that) = default;

  Certificate& operator=(const Certificate& that) {
    certificate_ = std::move(Copy(that).certificate_);
    return *this;
  }

  Certificate& operator=(Certificate&& that) {
    certificate_ = std::move(that.certificate_);
    return *this;
  }

  operator X509*() {
    return certificate_.get();
  }

 private:
  // Helper that copies a certificate so we can have value semantics.
  static Certificate Copy(const Certificate& from) {
    return Certificate(
        std::unique_ptr<X509, decltype(&X509_free)>(
            X509_dup(from.certificate_.get()),
            &X509_free));
  }

  std::unique_ptr<X509, decltype(&X509_free)> certificate_;
};

////////////////////////////////////////////////////////////////////////

// Builder for generating an X509 certificate.
//
// TODO(benh): current implementation of the builder is based off of
// code from Apache Mesos (specifically 3rdparty/libprocess) which
// most likely should be revisited and some of the existing functions
// should be removed and new ones (like a '.country_code()') should be
// added.
class CertificateBuilder {
 public:
  CertificateBuilder() {}

  eventuals::Expected::Of<Certificate> Build() &&;

  CertificateBuilder subject_key(rsa::Key&& key) && {
    subject_key_ = std::move(key);
    return std::move(*this);
  }

  CertificateBuilder sign_key(rsa::Key&& key) && {
    sign_key_ = std::move(key);
    return std::move(*this);
  }

  CertificateBuilder parent_certificate(Certificate&& certificate) && {
    parent_certificate_ = std::move(certificate);
    return std::move(*this);
  }

  CertificateBuilder serial(int serial) && {
    serial_ = serial;
    return std::move(*this);
  }

  CertificateBuilder days(int days) && {
    days_ = days;
    return std::move(*this);
  }

  CertificateBuilder hostname(std::string&& hostname) && {
    hostname_ = std::move(hostname);
    return std::move(*this);
  }

  CertificateBuilder ip(asio::ip::address&& ip) && {
    ip_ = std::move(ip);
    return std::move(*this);
  }

  CertificateBuilder organization_name(std::string&& name) && {
    organization_name_ = std::move(name);
    return std::move(*this);
  }

 private:
  // TODO(benh): check for missing subject/sign key at compile time.
  std::optional<rsa::Key> subject_key_;
  std::optional<rsa::Key> sign_key_;
  std::optional<Certificate> parent_certificate_;
  int serial_ = 1;
  int days_ = 365;
  std::optional<std::string> hostname_;
  std::optional<asio::ip::address> ip_;
  std::string organization_name_ = "Unknown";
};

////////////////////////////////////////////////////////////////////////

CertificateBuilder Certificate::Builder() {
  return CertificateBuilder();
}

////////////////////////////////////////////////////////////////////////

eventuals::Expected::Of<Certificate> CertificateBuilder::Build() && {
  // Using a 'using' here to reduce verbosity.
  using eventuals::Unexpected;

  if (!subject_key_.has_value()) {
    return Unexpected("Missing 'subject' key");
  }

  if (!sign_key_.has_value()) {
    return Unexpected("Missing 'sign' key");
  }

  std::optional<X509_NAME*> issuer_name;

  if (!parent_certificate_.has_value()) {
    // If there is no parent certificate, then the subject and
    // signing key must be the same.
    if (subject_key_ != sign_key_) {
      return Unexpected("Subject vs signing key mismatch");
    }
  } else {
    // If there is a parent certificate, then set the issuer name to
    // be that of the parent.
    issuer_name = X509_get_subject_name(parent_certificate_.value());

    if (issuer_name.value() == nullptr) {
      return Unexpected(
          "Failed to get subject name of parent certificate: "
          "X509_get_subject_name");
    }
  }

  // Allocate the in-memory structure for the certificate.
  X509* x509 = X509_new();
  if (x509 == nullptr) {
    return Unexpected("Failed to allocate certification: X509_new");
  }

  // Set the version to V3.
  if (X509_set_version(x509, 2) != 1) {
    X509_free(x509);
    return Unexpected("Failed to set version: X509_set_version");
  }

  // Set the serial number.
  if (ASN1_INTEGER_set(X509_get_serialNumber(x509), serial_) != 1) {
    X509_free(x509);
    return Unexpected("Failed to set serial number: ASN1_INTEGER_set");
  }

  // Make this certificate valid for 'days' number of days from now.
  if (X509_gmtime_adj(
          X509_get_notBefore(x509),
          0)
          == nullptr
      || X509_gmtime_adj(
             X509_get_notAfter(x509),
             60L * 60L * 24L * days_)
          == nullptr) {
    X509_free(x509);
    return Unexpected(
        "Failed to set valid days of certificate: X509_gmtime_adj");
  }

  // Set the public key for our certificate based on the subject key.
  if (X509_set_pubkey(x509, subject_key_.value()) != 1) {
    X509_free(x509);
    return Unexpected("Failed to set public key: X509_set_pubkey");
  }

  // Figure out our hostname if one was not provided.
  if (!hostname_.has_value()) {
    asio::error_code error;
    hostname_ = asio::ip::host_name(error);
    if (error) {
      X509_free(x509);
      return Unexpected("Failed to determine hostname");
    }
  }

  // Grab the subject name of the new certificate.
  X509_NAME* name = X509_get_subject_name(x509);
  if (name == nullptr) {
    X509_free(x509);
    return Unexpected("Failed to get subject name: X509_get_subject_name");
  }

  // Set the country code, organization, and common name.
  if (X509_NAME_add_entry_by_txt(
          name,
          "C",
          MBSTRING_ASC,
          // TODO(benh): add 'country_code()' to builder.
          reinterpret_cast<const unsigned char*>("US"),
          -1,
          -1,
          0)
      != 1) {
    X509_free(x509);
    return Unexpected("Failed to set country code: X509_NAME_add_entry_by_txt");
  }

  if (X509_NAME_add_entry_by_txt(
          name,
          "O",
          MBSTRING_ASC,
          reinterpret_cast<const unsigned char*>(organization_name_.c_str()),
          -1,
          -1,
          0)
      != 1) {
    X509_free(x509);
    return Unexpected(
        "Failed to set organization name: X509_NAME_add_entry_by_txt");
  }

  if (X509_NAME_add_entry_by_txt(
          name,
          "CN",
          MBSTRING_ASC,
          reinterpret_cast<const unsigned char*>(hostname_->c_str()),
          -1,
          -1,
          0)
      != 1) {
    X509_free(x509);
    return Unexpected("Failed to set common name: X509_NAME_add_entry_by_txt");
  }

  // Set the issuer name to be the same as the subject if it is not
  // already set (this is a self-signed certificate).
  if (!issuer_name.has_value()) {
    issuer_name = name;
  }

  CHECK(issuer_name.has_value());
  if (X509_set_issuer_name(x509, issuer_name.value()) != 1) {
    X509_free(x509);
    return Unexpected("Failed to set issuer name: X509_set_issuer_name");
  }

  if (ip_.has_value()) {
    // Add an X509 extension with an IP for subject alternative name.

    STACK_OF(GENERAL_NAME)* alt_name_stack = sk_GENERAL_NAME_new_null();
    if (alt_name_stack == nullptr) {
      X509_free(x509);
      return Unexpected("Failed to create a stack: sk_GENERAL_NAME_new_null");
    }

    GENERAL_NAME* alt_name = GENERAL_NAME_new();
    if (alt_name == nullptr) {
      sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
      X509_free(x509);
      return Unexpected("Failed to create GENERAL_NAME: GENERAL_NAME_new");
    }

    alt_name->type = GEN_IPADD;

    ASN1_STRING* alt_name_str = ASN1_STRING_new();
    if (alt_name_str == nullptr) {
      GENERAL_NAME_free(alt_name);
      sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
      X509_free(x509);
      return Unexpected("Failed to create alternative name: ASN1_STRING_new");
    }

    if (!ip_->is_v4()) {
      return Unexpected("Only IPv4 is currently supported");
    }

    in_addr in;
    in.s_addr = ip_->to_v4().to_ulong();

#ifdef __WINDOWS__
    // cURL defines `in_addr_t` as `unsigned long` for Windows,
    // so we do too for consistency.
    typedef unsigned long in_addr_t;
#endif // __WINDOWS__

    // For `iPAddress` we hand over a binary value as part of the
    // specification.
    if (ASN1_STRING_set(alt_name_str, &in.s_addr, sizeof(in_addr_t)) == 0) {
      ASN1_STRING_free(alt_name_str);
      GENERAL_NAME_free(alt_name);
      sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
      X509_free(x509);
      return Unexpected("Failed to set alternative name: ASN1_STRING_set");
    }

    // We are transferring ownership of 'alt_name_str` towards the
    // `ASN1_OCTET_STRING` here.
    alt_name->d.iPAddress = alt_name_str;

    // We try to transfer ownership of 'alt_name` towards the
    // `STACK_OF(GENERAL_NAME)` here.
    if (sk_GENERAL_NAME_push(alt_name_stack, alt_name) == 0) {
      GENERAL_NAME_free(alt_name);
      sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
      X509_free(x509);
      return Unexpected(
          "Failed to push alternative name: sk_GENERAL_NAME_push");
    }

    // We try to transfer the ownership of `alt_name_stack` towards the
    // `X509` here.
    if (X509_add1_ext_i2d(
            x509,
            NID_subject_alt_name,
            alt_name_stack,
            0,
            0)
        == 0) {
      sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
      X509_free(x509);
      return Unexpected(
          "Failed to set subject alternative name: X509_add1_ext_i2d");
    }

    sk_GENERAL_NAME_pop_free(alt_name_stack, GENERAL_NAME_free);
  }

  // Sign the certificate with the sign key.
  if (X509_sign(x509, sign_key_.value(), EVP_sha1()) == 0) {
    X509_free(x509);
    return Unexpected("Failed to sign certificate: X509_sign");
  }

  return Certificate(
      std::unique_ptr<X509, decltype(&X509_free)>(x509, &X509_free));
}

////////////////////////////////////////////////////////////////////////

} // namespace x509

////////////////////////////////////////////////////////////////////////

namespace pem {

////////////////////////////////////////////////////////////////////////

// Returns an expected 'std::string' with the encoded X509 certificate in
// PEM format or an unexpected.
eventuals::Expected::Of<std::string> Encode(X509* certificate) {
  BIO* bio = BIO_new(BIO_s_mem());

  int write = PEM_write_bio_X509(bio, certificate);

  if (write != 1) {
    return eventuals::Unexpected("Failed to write certificate to memory");
  }

  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio, &memory);

  std::string result(memory->data, memory->length);

  BIO_free(bio);

  return std::move(result);
}

////////////////////////////////////////////////////////////////////////

} // namespace pem

////////////////////////////////////////////////////////////////////////
