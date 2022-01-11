#include "eventuals/expected.h"
#include "openssl/rsa.h"

////////////////////////////////////////////////////////////////////////

namespace rsa {

////////////////////////////////////////////////////////////////////////

// Builder for generating an RSA private key.
class Builder {
 public:
  Builder() {}

  eventuals::Expected::Of<EVP_PKEY*> Build() &&;

  Builder bits(int bits) && {
    bits_ = bits;
    return std::move(*this);
  }

  Builder exponent(unsigned long exponent) && {
    exponent_ = exponent;
    return std::move(*this);
  }

 private:
  int bits_ = 2048;
  unsigned long exponent_ = RSA_F4;
};

////////////////////////////////////////////////////////////////////////

eventuals::Expected::Of<EVP_PKEY*> Builder::Build() && {
  // Using a 'using' here to reduce verbosity.
  using eventuals::Unexpected;

  // Allocate the in-memory structure for the private key.
  EVP_PKEY* private_key = EVP_PKEY_new();
  if (private_key == nullptr) {
    return Unexpected("Failed to allocate key: EVP_PKEY_new");
  }

  // Allocate space for the exponent.
  BIGNUM* exponent = BN_new();
  if (exponent == nullptr) {
    EVP_PKEY_free(private_key);
    return Unexpected("Failed to allocate exponent: BN_new");
  }

  // Assign the exponent.
  if (BN_set_word(exponent, exponent_) != 1) {
    BN_free(exponent);
    EVP_PKEY_free(private_key);
    return Unexpected("Failed to set exponent: BN_set_word");
  }

  // Allocate the in-memory structure for the key pair.
  RSA* rsa = RSA_new();
  if (rsa == nullptr) {
    BN_free(exponent);
    EVP_PKEY_free(private_key);
    return Unexpected("Failed to allocate RSA: RSA_new");
  }

  // Generate the RSA key pair.
  if (RSA_generate_key_ex(rsa, bits_, exponent, nullptr) != 1) {
    RSA_free(rsa);
    BN_free(exponent);
    EVP_PKEY_free(private_key);
    return Unexpected(ERR_error_string(ERR_get_error(), nullptr));
  }

  // We no longer need the exponent, so let's free it.
  BN_free(exponent);

  // Associate the RSA key with the private key. If this association
  // is successful, then the RSA key will be freed when the private
  // key is freed.
  if (EVP_PKEY_assign_RSA(private_key, rsa) != 1) {
    RSA_free(rsa);
    EVP_PKEY_free(private_key);
    return Unexpected("Failed to assign RSA key: EVP_PKEY_assign_RSA");
  }

  return private_key;
}

////////////////////////////////////////////////////////////////////////

} // namespace rsa

////////////////////////////////////////////////////////////////////////
