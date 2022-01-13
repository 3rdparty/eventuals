#pragma once

#include "eventuals/expected.h"
#include "openssl/bio.h"
#include "openssl/pem.h"

////////////////////////////////////////////////////////////////////////

namespace pem {

////////////////////////////////////////////////////////////////////////

// Returns an expected 'std::string' with the encoded private key in
// PEM format or an unexpected.
eventuals::Expected::Of<std::string> Encode(EVP_PKEY* key) {
  BIO* bio = BIO_new(BIO_s_mem());

  int write = PEM_write_bio_PrivateKey(
      bio,
      key,
      nullptr,
      nullptr,
      0,
      0,
      nullptr);

  if (write != 1) {
    return eventuals::Unexpected("Failed to write private key to memory");
  }

  BUF_MEM* memory = nullptr;
  BIO_get_mem_ptr(bio, &memory);

  std::string result(memory->data, memory->length);

  BIO_free(bio);

  return std::move(result);
}

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
