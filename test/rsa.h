#pragma once

#include "eventuals/expected.h"
#include "openssl/rsa.h"

////////////////////////////////////////////////////////////////////////

namespace rsa {

////////////////////////////////////////////////////////////////////////

// Forward declaration.
class KeyBuilder;

////////////////////////////////////////////////////////////////////////

class Key {
 public:
  static KeyBuilder Builder();

  Key(const Key& that)
    : Key(Copy(that.key_.get())) {}

  Key(Key&& that) = default;

  Key& operator=(const Key& that) {
    key_.reset(Copy(that.key_.get()));
    return *this;
  }

  Key& operator=(Key&& that) {
    key_ = std::move(that.key_);
    return *this;
  }

  bool operator==(const Key& that) const {
    return EVP_PKEY_cmp(key_.get(), that.key_.get());
  }

  bool operator!=(const Key& that) const {
    return !operator==(that);
  }

  operator EVP_PKEY*() {
    return key_.get();
  }

 private:
  // Helper that copies a key so we can have value semantics.
  static EVP_PKEY* Copy(EVP_PKEY* from) {
    // TODO(benh): find a better way to copy an EVP_PKEY* without encoding
    // to memory as PEM and then decoding again!!!
    BIO* bio = BIO_new(BIO_s_mem());
    CHECK_EQ(
        PEM_write_bio_PrivateKey(bio, from, nullptr, nullptr, 0, 0, nullptr),
        1);
    EVP_PKEY* copy = CHECK_NOTNULL(
        PEM_read_bio_PrivateKey(bio, nullptr, 0, nullptr));
    BIO_free(bio);
    return copy;
  }

  friend class KeyBuilder;

  Key(EVP_PKEY* key)
    : key_(key, &EVP_PKEY_free) {}

  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key_;
};

////////////////////////////////////////////////////////////////////////

// Builder for generating an RSA private key.
class KeyBuilder {
 public:
  KeyBuilder() {}

  eventuals::Expected::Of<Key> Build() &&;

  KeyBuilder bits(int bits) && {
    bits_ = bits;
    return std::move(*this);
  }

  KeyBuilder exponent(unsigned long exponent) && {
    exponent_ = exponent;
    return std::move(*this);
  }

 private:
  int bits_ = 2048;
  unsigned long exponent_ = RSA_F4;
};

////////////////////////////////////////////////////////////////////////

KeyBuilder Key::Builder() {
  return KeyBuilder();
}

////////////////////////////////////////////////////////////////////////

eventuals::Expected::Of<Key> KeyBuilder::Build() && {
  // Using a 'using' here to reduce verbosity.
  using eventuals::Unexpected;

  // Allocate the in-memory structure for the private key.
  EVP_PKEY* key = EVP_PKEY_new();
  if (key == nullptr) {
    return Unexpected("Failed to allocate key: EVP_PKEY_new");
  }

  // Allocate space for the exponent.
  BIGNUM* exponent = BN_new();
  if (exponent == nullptr) {
    EVP_PKEY_free(key);
    return Unexpected("Failed to allocate exponent: BN_new");
  }

  // Assign the exponent.
  if (BN_set_word(exponent, exponent_) != 1) {
    BN_free(exponent);
    EVP_PKEY_free(key);
    return Unexpected("Failed to set exponent: BN_set_word");
  }

  // Allocate the in-memory structure for the key pair.
  RSA* rsa = RSA_new();
  if (rsa == nullptr) {
    BN_free(exponent);
    EVP_PKEY_free(key);
    return Unexpected("Failed to allocate RSA: RSA_new");
  }

  // Generate the RSA key pair.
  if (RSA_generate_key_ex(rsa, bits_, exponent, nullptr) != 1) {
    RSA_free(rsa);
    BN_free(exponent);
    EVP_PKEY_free(key);
    return Unexpected(ERR_error_string(ERR_get_error(), nullptr));
  }

  // We no longer need the exponent, so let's free it.
  BN_free(exponent);

  // Associate the RSA key with the private key. If this association
  // is successful, then the RSA key will be freed when the private
  // key is freed.
  if (EVP_PKEY_assign_RSA(key, rsa) != 1) {
    RSA_free(rsa);
    EVP_PKEY_free(key);
    return Unexpected("Failed to assign RSA key: EVP_PKEY_assign_RSA");
  }

  return Key(key);
}

////////////////////////////////////////////////////////////////////////

} // namespace rsa

////////////////////////////////////////////////////////////////////////
