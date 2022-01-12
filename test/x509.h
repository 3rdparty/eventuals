#pragma once

#include "eventuals/expected.h"
#include "openssl/x509.h"

////////////////////////////////////////////////////////////////////////

namespace x509 {

////////////////////////////////////////////////////////////////////////

// Builder for generating an X509 certificate.
class Builder {
 public:
  Builder() {}

  eventuals::Expected::Of<X509*> Build() &&;

  Builder subject_key(EVP_PKEY* key) && {
    subject_key_ = key;
    return std::move(*this);
  }

  Builder sign_key(EVP_PKEY* key) && {
    sign_key_ = key;
    return std::move(*this);
  }

  Builder parent_certificate(X509* certificate) && {
    parent_certificate_ = certificate;
    return std::move(*this);
  }

  Builder serial(int serial) && {
    serial_ = serial;
    return std::move(*this);
  }

  Builder days(int days) && {
    days_ = days;
    return std::move(*this);
  }

  Builder hostname(std::string&& hostname) && {
    hostname_ = std::move(hostname);
    return std::move(*this);
  }

  Builder ip(asio::ip::address&& ip) && {
    ip_ = std::move(ip);
    return std::move(*this);
  }

 private:
  EVP_PKEY* subject_key_;
  EVP_PKEY* sign_key_;
  std::optional<X509*> parent_certificate_;
  int serial_ = 1;
  int days_ = 365;
  std::optional<std::string> hostname_;
  std::optional<asio::ip::address> ip_;
};

////////////////////////////////////////////////////////////////////////

eventuals::Expected::Of<X509*> Builder::Build() && {
  // Using a 'using' here to reduce verbosity.
  using eventuals::Unexpected;

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
  if (X509_set_pubkey(x509, subject_key_) != 1) {
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
          reinterpret_cast<const unsigned char*>("Test"),
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
  if (X509_sign(x509, sign_key_, EVP_sha1()) == 0) {
    X509_free(x509);
    return Unexpected("Failed to sign certificate: X509_sign");
  }

  return x509;
}

////////////////////////////////////////////////////////////////////////

} // namespace x509

////////////////////////////////////////////////////////////////////////
