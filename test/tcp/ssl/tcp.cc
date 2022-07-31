#include "test/tcp/ssl/tcp.h"

namespace eventuals::test {

using namespace eventuals::ip::tcp::ssl;

SSLContext TCPSSLTest::SetupSSLContextClient() {
  return SSLContext::Builder()
      .ssl_version(SSLVersion::TLSv1_2_CLIENT)
      .certificate_chain(pem_certificate().c_str(), pem_certificate().length())
      .Build();
}

SSLContext TCPSSLTest::SetupSSLContextServer() {
  return SSLContext::Builder()
      .ssl_version(SSLVersion::TLSv1_2_SERVER)
      .private_key(pem_key().c_str(), pem_key().length(), FileFormat::PEM)
      .certificate_chain(pem_certificate().c_str(), pem_certificate().length())
      .Build();
}

// NOTE: We are using static variables to prevent regeneration
// of keys and certificates on every call.
const rsa::Key& TCPSSLTest::rsa_key() {
  static auto key_expected = rsa::Key::Builder().Build();

  CHECK(key_expected) << "Failed to generate RSA private key";

  static auto key = *key_expected;

  return key;
}

const std::string& TCPSSLTest::pem_key() {
  static auto pem_key_expected = pem::Encode(rsa::Key(rsa_key()));

  CHECK(pem_key_expected) << "Failed to PEM encode RSA private key";

  static auto pem_key = *pem_key_expected;

  return pem_key;
}

const x509::Certificate& TCPSSLTest::certificate() {
  static auto certificate_expected = x509::Certificate::Builder()
                                         .subject_key(rsa::Key(rsa_key()))
                                         .sign_key(rsa::Key(rsa_key()))
                                         .hostname(host())
                                         .Build();

  CHECK(certificate_expected) << "Failed to generate X509 certificate";

  static auto certificate = *certificate_expected;

  return certificate;
}

const std::string& TCPSSLTest::pem_certificate() {
  static auto pem_certificate_expected =
      pem::Encode(x509::Certificate(certificate()));

  CHECK(pem_certificate_expected) << "Failed to PEM encode X509 certificate";

  static auto pem_certificate = *pem_certificate_expected;

  return pem_certificate;
}

} // namespace eventuals::test
