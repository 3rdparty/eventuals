#pragma once

#include "eventuals/expected.h"
#include "openssl/pem.h"

////////////////////////////////////////////////////////////////////////

namespace pem {

////////////////////////////////////////////////////////////////////////

// Returns an expected 'std::string' with the encoded private key in
// PEM format or an unexpected.
eventuals::Expected::Of<std::string> Encode(EVP_PKEY* key) {
  char buffer[8192];

  FILE* file = CHECK_NOTNULL(fmemopen(buffer, 8192, "wb"));

  int write = PEM_write_PrivateKey(
      file,
      key,
      nullptr,
      nullptr,
      0,
      nullptr,
      nullptr);

  if (write != 1) {
    return eventuals::Unexpected("Failed to write private key to memory");
  }

  // Flush the file pointer so we can correctly determine the size of
  // the encoded string.
  fflush(file);

  long size = ftell(file);

  fclose(file);

  return std::string(buffer, size);
}

////////////////////////////////////////////////////////////////////////

// Returns an expected 'std::string' with the encoded X509 certificate in
// PEM format or an unexpected.
eventuals::Expected::Of<std::string> Encode(X509* certificate) {
  char buffer[8192];

  FILE* file = CHECK_NOTNULL(fmemopen(buffer, 8192, "wb"));

  int write = PEM_write_X509(file, certificate);

  if (write != 1) {
    return eventuals::Unexpected("Failed to write certificate to memory");
  }

  // Flush the file pointer so we can correctly determine the size of
  // the encoded string.
  fflush(file);

  long size = ftell(file);

  fclose(file);

  return std::string(buffer, size);
}

////////////////////////////////////////////////////////////////////////

} // namespace pem

////////////////////////////////////////////////////////////////////////
