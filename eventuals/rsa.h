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

class Key final {
 public:
  static auto Builder();

#if _WIN32
  // NOTE: default constructor should not exist or be used but is
  // necessary on Windows so this type can be used as a type parameter
  // to 'std::promise', see: https://bit.ly/VisualStudioStdPromiseBug
  Key()
    : key_(nullptr, &EVP_PKEY_free) {}
#else
  Key() = delete;
#endif

  Key(std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key)
    : key_(std::move(key)) {
    CHECK_EQ(EVP_PKEY_id(CHECK_NOTNULL(key_.get())), EVP_PKEY_RSA);
  }

  Key(const Key& that)
    : Key(Copy(that)) {}

  Key(Key&& that) = default;

  Key& operator=(const Key& that) {
    key_ = std::move(Copy(that).key_);
    return *this;
  }

  Key& operator=(Key&& that) noexcept {
    if (this == &that) {
      return *this;
    }

    key_ = std::move(that.key_);
    return *this;
  }

  bool operator==(const Key& that) const {
    return EVP_PKEY_cmp(
        CHECK_NOTNULL(key_.get()),
        CHECK_NOTNULL(that.key_.get()));
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
    // TODO(alexmc, rjh): the following code uses deprecated APIs, including
    // RSAPrivateKey_dup, EVP_PKEY_get1_RSA, and EVP_PKEY_set1_RSA. We should
    // port to a replacement, which looks like EVP_PKEY_fromdata.
    // See:
    // https://www.openssl.org/docs/manmaster/man3/RSAPrivateKey_dup.html
    // https://www.openssl.org/docs/manmaster/man3/EVP_PKEY_get0_RSA.html
    // https://www.openssl.org/docs/manmaster/man3/EVP_PKEY_set1_RSA.html
    // and:
    // https://www.openssl.org/docs/manmaster/man3/EVP_PKEY_fromdata.html

    // Get the underlying RSA key.
    CHECK_EQ(EVP_PKEY_id(CHECK_NOTNULL(from.key_.get())), EVP_PKEY_RSA);
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
class Key::_Builder final : public builder::Builder {
 public:
  ~_Builder() override = default;

  auto bits(int bits) && {
    static_assert(!has_bits_, "Duplicate 'bits'");
    return Construct<_Builder>(
        bits_.Set(bits),
        std::move(exponent_));
  }

  auto exponent(unsigned long exponent) && {
    static_assert(!has_exponent_, "Duplicate 'exponent'");
    return Construct<_Builder>(
        std::move(bits_),
        exponent_.Set(exponent));
  }

  eventuals::expected<Key> Build() &&;

 private:
  friend class builder::Builder;
  friend class Key;

  _Builder() = default;

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
eventuals::expected<Key> Key::_Builder<
    has_bits_,
    has_exponent_>::Build() && {
  // Allocate the in-memory structure for the private key.
  EVP_PKEY* key = EVP_PKEY_new();
  if (key == nullptr) {
    return eventuals::make_unexpected(
        "Failed to allocate key: EVP_PKEY_new");
  }

  // Allocate space for the exponent.
  BIGNUM* exponent = BN_new();
  if (exponent == nullptr) {
    EVP_PKEY_free(key);
    return eventuals::make_unexpected(
        "Failed to allocate exponent: BN_new");
  }

  // Assign the exponent.
  if (BN_set_word(exponent, exponent_.value()) != 1) {
    BN_free(exponent);
    EVP_PKEY_free(key);
    return eventuals::make_unexpected(
        "Failed to set exponent: BN_set_word");
  }

  // Allocate the in-memory structure for the key pair.
  RSA* rsa = RSA_new();
  if (rsa == nullptr) {
    BN_free(exponent);
    EVP_PKEY_free(key);
    return eventuals::make_unexpected(
        "Failed to allocate RSA: RSA_new");
  }

  // Generate the RSA key pair.
  if (RSA_generate_key_ex(rsa, bits_.value(), exponent, nullptr) != 1) {
    RSA_free(rsa);
    BN_free(exponent);
    EVP_PKEY_free(key);
    return eventuals::make_unexpected(
        ERR_error_string(ERR_get_error(), nullptr));
  }

  // We no longer need the exponent, so let's free it.
  BN_free(exponent);

  // Associate the RSA key with the private key. If this association
  // is successful, then the RSA key will be freed when the private
  // key is freed.
  if (EVP_PKEY_assign_RSA(key, rsa) != 1) {
    RSA_free(rsa);
    EVP_PKEY_free(key);
    return eventuals::make_unexpected(
        "Failed to assign RSA key: EVP_PKEY_assign_RSA");
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
inline eventuals::expected<std::string> Encode(EVP_PKEY* key) {
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
    return eventuals::make_unexpected(
        "Failed to write private key to memory");
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
