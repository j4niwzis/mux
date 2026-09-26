// SPDX-License-Identifier: AGPL-3.0-only
// A self-signed certificate made for a test: a P-256 key and a certificate
// for 127.0.0.1 and localhost, valid for a day, in PEM.
module;

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

export module mux.test.certificate;

import std;

export namespace mux::test {

struct certificate {
  std::string certificate_pem;
  std::string key_pem;
};

inline std::string pem_of(auto write) {
  BIO* out = ::BIO_new(::BIO_s_mem());
  write(out);
  char* data = nullptr;
  const long length = ::BIO_get_mem_data(out, &data);
  std::string made(data, static_cast<std::size_t>(length));
  ::BIO_free(out);
  return made;
}

inline certificate self_signed() {
  EVP_PKEY* key = ::EVP_EC_gen("P-256");
  X509* made = ::X509_new();
  ::X509_set_version(made, 2);
  ::ASN1_INTEGER_set(::X509_get_serialNumber(made), 1);
  ::X509_gmtime_adj(::X509_getm_notBefore(made), -60);
  ::X509_gmtime_adj(::X509_getm_notAfter(made), 24 * 60 * 60);
  ::X509_set_pubkey(made, key);
  X509_NAME* name = ::X509_get_subject_name(made);
  ::X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1,
                               0);
  ::X509_set_issuer_name(made, name);
  X509V3_CTX context;
  X509V3_set_ctx(&context, made, made, nullptr, nullptr, 0);
  for (const auto& [nid, value] : {std::pair{NID_subject_alt_name, "IP:127.0.0.1,DNS:localhost"},
                                   std::pair{NID_basic_constraints, "critical,CA:TRUE"}}) {
    X509_EXTENSION* extension = ::X509V3_EXT_conf_nid(nullptr, &context, nid, value);
    ::X509_add_ext(made, extension, -1);
    ::X509_EXTENSION_free(extension);
  }
  ::X509_sign(made, key, ::EVP_sha256());
  certificate out{pem_of([&](BIO* to) { ::PEM_write_bio_X509(to, made); }),
                  pem_of([&](BIO* to) { ::PEM_write_bio_PrivateKey(to, key, nullptr, nullptr, 0, nullptr, nullptr); })};
  ::X509_free(made);
  ::EVP_PKEY_free(key);
  return out;
}

}  // namespace mux::test
