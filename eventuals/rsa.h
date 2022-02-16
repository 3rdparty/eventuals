#pragma once

#include "openssl/bio.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/rsa.h"

// Must be included after openssl includes.
#include "eventuals/builder.h"
#include "eventuals/expected.h"

////////////////////////////////////////////////////////////////////////

namespace rsa {

////////////////////////////////////////////////////////////////////////

class Key {
 public:
  static auto Builder();

  Key(std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key)
    : key_(std::move(key)) {
    CHECK_EQ(CHECK_NOTNULL(key_.get())->type, EVP_PKEY_RSA);
  }

  Key(const Key& that)
    : Key(Copy(that)) {}

  Key(Key&& that) = default;

  Key& operator=(const Key& that) {
    key_ = std::move(Copy(that).key_);
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
    return CHECK_NOTNULL(key_.get());
  }

 private:
  template <bool, bool>
  class _Builder;

  // Helper that copies a key so we can have value semantics.
  static Key Copy(const Key& from) {
    // Get the underlying RSA key.
    CHECK_EQ(CHECK_NOTNULL(from.key_.get())->type, EVP_PKEY_RSA);
    RSA* rsa = EVP_PKEY_get1_RSA(from.key_.get());

    EVP_PKEY* to = EVP_PKEY_new();

    // Duplicate the RSA key.
    RSA* duplicate = RSAPrivateKey_dup(rsa);
    EVP_PKEY_set1_RSA(to, duplicate);
    RSA_free(duplicate);

    // Decrement reference count incremented from calling
    // 'EVP_PKEY_get1_RSA()'.
    RSA_free(rsa);

    return Key(
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
            to,
            &EVP_PKEY_free));
  }

  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key_;
};

////////////////////////////////////////////////////////////////////////

// Builder for generating an RSA private key.
template <bool has_bits_, bool has_exponent_>
class Key::_Builder : public builder::Builder {
 public:
  auto bits(int bits) && {
    static_assert(!has_bits_, "Duplicate 'bits'");
    return Construct<_Builder>(
        bits_.Set(std::move(bits)),
        std::move(exponent_));
  }

  auto exponent(unsigned long exponent) && {
    static_assert(!has_exponent_, "Duplicate 'exponent'");
    return Construct<_Builder>(
        std::move(bits_),
        exponent_.Set(std::move(exponent)));
  }

  eventuals::Expected::Of<Key> Build() &&;

 private:
  friend class builder::Builder;
  friend class Key;

  _Builder() {}

  _Builder(
      builder::FieldWithDefault<int, has_bits_> bits,
      builder::FieldWithDefault<unsigned long, has_exponent_> exponent)
    : bits_(std::move(bits)),
      exponent_(std::move(exponent)) {}

  builder::FieldWithDefault<int, has_bits_> bits_ = 2048;
  builder::FieldWithDefault<unsigned long, has_exponent_> exponent_ = RSA_F4;
};

////////////////////////////////////////////////////////////////////////

inline auto Key::Builder() {
  return Key::_Builder<false, false>();
}

////////////////////////////////////////////////////////////////////////

template <bool has_bits_, bool has_exponent_>
eventuals::Expected::Of<Key> Key::_Builder<
    has_bits_,
    has_exponent_>::Build() && {
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
  if (BN_set_word(exponent, exponent_.value()) != 1) {
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
  if (RSA_generate_key_ex(rsa, bits_.value(), exponent, nullptr) != 1) {
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

  return Key(
      std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(key, &EVP_PKEY_free));
}

////////////////////////////////////////////////////////////////////////

} // namespace rsa

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

} // namespace pem

////////////////////////////////////////////////////////////////////////
