// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.crypto:keys -- Files sealed and opened, room keys exported, cross-signing, secret storage and the key backup.
module;
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <rust/cxx.h>
#include <vodozemac/src/lib.rs.h>
export module mux.proto.matrix.crypto:keys;

import std;
import splice;
import knot;
import loom.ev;
import loom.cs.keys;
import mux.vault;
import splice.bytes;
import :wire;

export namespace mux::proto::matrix::client::crypto {
namespace detail {
template <class Bytes>
concept byte_range = std::ranges::input_range<Bytes> && std::same_as<std::ranges::range_value_t<Bytes>, std::uint8_t>;
// A cipher run over any range of bytes, a piece at a time (CTR, CBC): its
// key and counter taken as exactly the sizes they are.
template <byte_range Key, byte_range Iv, byte_range In>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> cipher(const EVP_CIPHER* kind, bool encrypt, Key&& key, Iv&& iv,
                                                                     In&& in) {
  const auto k = splice::bytes::exactly<32>(std::forward<Key>(key));
  const auto v = splice::bytes::exactly<16>(std::forward<Iv>(iv));
  if (!k || !v)
    return std::nullopt;
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
  if (!ctx || EVP_CipherInit_ex(ctx.get(), kind, nullptr, k->data(), v->data(), encrypt ? 1 : 0) != 1)
    return std::nullopt;
  std::vector<std::uint8_t> out;
  bool fine = true;
  splice::bytes::in_pieces(std::forward<In>(in), [&](std::span<const std::uint8_t> piece) {
    const std::size_t at = out.size();
    out.resize(at + piece.size() + 32);
    int len = 0;
    fine = fine && EVP_CipherUpdate(ctx.get(), out.data() + at, &len, piece.data(), static_cast<int>(piece.size())) == 1;
    out.resize(at + static_cast<std::size_t>(len));
  });
  const std::size_t at = out.size();
  out.resize(at + 32);
  int tail = 0;
  if (!fine || EVP_CipherFinal_ex(ctx.get(), out.data() + at, &tail) != 1)
    return std::nullopt;
  out.resize(at + static_cast<std::size_t>(tail));
  return out;
}
// AES-256-CTR, either way: the same operation.
template <byte_range Key, byte_range Iv, byte_range In>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> aes_ctr(Key&& key, Iv&& iv, In&& in) {
  return cipher(EVP_aes_256_ctr(), true, std::forward<Key>(key), std::forward<Iv>(iv), std::forward<In>(in));
}
// SHA-256 of any ranges of bytes, one after another.
template <byte_range... Parts>
[[nodiscard]] inline std::array<std::uint8_t, 32> sha256(Parts&&... parts) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  bool fine = md && EVP_DigestInit_ex(md.get(), EVP_sha256(), nullptr) == 1;
  (splice::bytes::in_pieces(std::forward<Parts>(parts), [&](std::span<const std::uint8_t> piece) {
     fine = fine && EVP_DigestUpdate(md.get(), piece.data(), piece.size()) == 1;
   }),
   ...);
  std::array<std::uint8_t, 32> out{};
  unsigned int size = 0;
  if (!fine || EVP_DigestFinal_ex(md.get(), out.data(), &size) != 1 || size != out.size())
    throw std::runtime_error("SHA-256 failed");
  return out;
}
[[nodiscard]] inline std::string url_safe(std::string text) {
  return text | std::views::transform([](char c) { return c == '+' ? '-' : c == '/' ? '_' : c; }) | std::ranges::to<std::string>();
}
}  // namespace detail

// A file sealed for an encrypted room: its ciphertext, and what opens it --
// its URL filled in once it is uploaded.
struct sealed_file {
  std::vector<std::uint8_t> bytes;
  encrypted_file info;
};
template <detail::byte_range Plain>
[[nodiscard]] inline sealed_file seal_file(Plain&& plain) {
  const auto key = mux::vault::vault::random(32);
  // The counter's start: 8 random bytes, then a zero counter (as the spec
  // has it, so that no file is long enough to wrap it).
  auto iv = mux::vault::vault::random(8);
  iv.resize(16, 0);
  auto ciphertext = detail::aes_ctr(key, iv, std::forward<Plain>(plain));
  if (!ciphertext)
    throw std::runtime_error("the file could not be encrypted");
  const auto hash = detail::sha256(*ciphertext);
  return sealed_file{.bytes = std::move(*ciphertext),
                     .info = encrypted_file{.key = jwk{.k = detail::url_safe(splice::bytes::base64_text(key))},
                                            .iv = splice::bytes::base64_text(iv),
                                            .hashes = {{"sha256", splice::bytes::base64_text(hash)}}}};
}
// A file opened: none where it is not what its event says -- its hash
// another, its key or counter not AES-256-CTR's.
// The sealed bytes walked twice -- hashed, then decrypted -- never copied.
template <class Sealed>
  requires detail::byte_range<Sealed> && std::ranges::forward_range<Sealed>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> open_file(Sealed&& sealed, const encrypted_file& info) {
  if (info.key.alg != "A256CTR" || info.key.kty != "oct")
    return std::nullopt;
  const auto key = from_base64(info.key.k);
  const auto iv = from_base64(info.iv);
  const auto said = info.hashes.find("sha256");
  if (!key || !iv || said == info.hashes.end())
    return std::nullopt;
  const auto expected = from_base64(said->second);
  const auto hash = detail::sha256(sealed);
  if (!expected || !std::ranges::equal(*expected, hash))
    return std::nullopt;
  return detail::aes_ctr(*key, *iv, sealed);
}

// Room keys as Element exports them (the spec's key export format): each
// session's room, where it came from, and its key from the first message it
// can read.
struct exported_session {
  std::string algorithm = "m.megolm.v1.aes-sha2";
  std::vector<std::string> forwarding_curve25519_key_chain;
  std::string room_id;
  std::string sender_key;
  std::map<std::string, std::string> sender_claimed_keys;
  std::string session_id;
  std::string session_key;
  friend consteval auto json_schema(knot::type<exported_session>) { return knot::schema<exported_session>(); }
};

namespace detail {
inline constexpr std::string_view kExportHeader = "-----BEGIN MEGOLM SESSION DATA-----";
inline constexpr std::string_view kExportFooter = "-----END MEGOLM SESSION DATA-----";
inline constexpr std::uint32_t kExportRounds = 500000;
// The AES key and the HMAC key a passphrase makes with a salt: PBKDF2,
// HMAC-SHA-512, 64 bytes.
template <byte_range Salt>
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 64>> export_keys_of(std::string_view passphrase, Salt&& salted,
                                                                              std::uint32_t rounds) {
  std::array<std::uint8_t, 64> out{};
  const std::string secret(passphrase);
  const auto salt = splice::bytes::buffer_of(std::forward<Salt>(salted));  // PBKDF2 reads it whole
  if (PKCS5_PBKDF2_HMAC(secret.data(), static_cast<int>(secret.size()), salt.data(), static_cast<int>(salt.size()),
                        static_cast<int>(rounds), EVP_sha512(), static_cast<int>(out.size()), out.data()) != 1)
    return std::nullopt;
  return out;
}
// HMAC-SHA-256 of any ranges of bytes, one after another, under a key.
template <byte_range Key, byte_range... Parts>
[[nodiscard]] inline std::array<std::uint8_t, 32> hmac_sha256(Key&& key, Parts&&... parts) {
  const auto secret = splice::bytes::buffer_of(std::forward<Key>(key));  // OpenSSL keeps the key whole
  std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr), &EVP_MAC_free);
  std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> ctx(mac ? EVP_MAC_CTX_new(mac.get()) : nullptr, &EVP_MAC_CTX_free);
  std::string digest = "SHA256";
  const OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string("digest", digest.data(), 0), OSSL_PARAM_construct_end()};
  bool fine = ctx && EVP_MAC_init(ctx.get(), secret.data(), secret.size(), params) == 1;
  (splice::bytes::in_pieces(std::forward<Parts>(parts), [&](std::span<const std::uint8_t> piece) {
     fine = fine && EVP_MAC_update(ctx.get(), piece.data(), piece.size()) == 1;
   }),
   ...);
  std::array<std::uint8_t, 32> out{};
  std::size_t size = 0;
  if (!fine || EVP_MAC_final(ctx.get(), out.data(), &size, out.size()) != 1 || size != out.size())
    throw std::runtime_error("HMAC-SHA-256 failed");
  return out;
}
}  // namespace detail

// Sessions sealed under a passphrase, as a file Element reads: version 1,
// a salt, a counter's start with bit 63 clear, the rounds, the ciphertext,
// and an HMAC-SHA-256 of all of it -- in base64, between its two lines.
[[nodiscard]] inline std::string export_file(const std::vector<exported_session>& sessions, std::string_view passphrase) {
  const auto salt = mux::vault::vault::random(16);
  auto iv = mux::vault::vault::random(16);
  iv[8] &= 0x7F;
  const auto keys = detail::export_keys_of(passphrase, salt, detail::kExportRounds);
  if (!keys)
    throw std::runtime_error("the key could not be made from the passphrase");
  const auto ciphertext = detail::aes_ctr(std::span(*keys).first(32), iv, splice::bytes::of(knot::to_json(sessions)));
  if (!ciphertext)
    throw std::runtime_error("the keys could not be encrypted");
  std::vector<std::uint8_t> body{1};
  body.insert(body.end(), salt.begin(), salt.end());
  body.insert(body.end(), iv.begin(), iv.end());
  for (const int shift : {24, 16, 8, 0})
    body.push_back(static_cast<std::uint8_t>((detail::kExportRounds >> shift) & 0xFF));
  body.insert(body.end(), ciphertext->begin(), ciphertext->end());
  const auto mac = detail::hmac_sha256(std::span(*keys).last(32), body);
  body.insert(body.end(), mac.begin(), mac.end());
  const std::string lines = splice::bytes::every(splice::bytes::base64_padded(body), 96, '\n') | std::ranges::to<std::string>();
  return std::format("{}\n{}\n{}\n", detail::kExportHeader, lines, detail::kExportFooter);
}
// A key file opened: none where it is not one, its passphrase another, or
// it was changed (its HMAC). Its rounds bounded, as anyone could write them.
[[nodiscard]] inline std::optional<std::vector<exported_session>> import_file(std::string_view text, std::string_view passphrase) {
  const auto begin = text.find(detail::kExportHeader);
  const auto end = text.find(detail::kExportFooter);
  if (begin == std::string_view::npos || end == std::string_view::npos || end < begin)
    return std::nullopt;
  const std::string encoded = text.substr(begin + detail::kExportHeader.size(), end - begin - detail::kExportHeader.size()) |
                              std::views::filter([](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; }) |
                              std::ranges::to<std::string>();
  const auto body = from_base64(encoded);
  if (!body || body->size() < 1 + 16 + 16 + 4 + 32 || (*body)[0] != 1)
    return std::nullopt;
  const std::span<const std::uint8_t> all(*body);
  const auto salt = all.subspan(1, 16);
  const auto iv = all.subspan(17, 16);
  const std::uint32_t rounds = (std::uint32_t{all[33]} << 24) | (std::uint32_t{all[34]} << 16) | (std::uint32_t{all[35]} << 8) |
                               std::uint32_t{all[36]};
  if (rounds < 1 || rounds > 10'000'000)
    return std::nullopt;
  const auto signed_part = all.first(all.size() - 32);
  const auto mac = all.last(32);
  const auto keys = detail::export_keys_of(passphrase, salt, rounds);
  if (!keys)
    return std::nullopt;
  const auto expected = detail::hmac_sha256(std::span(*keys).last(32), signed_part);
  if (CRYPTO_memcmp(expected.data(), mac.data(), 32) != 0)
    return std::nullopt;
  const auto plain = detail::aes_ctr(std::span(*keys).first(32), iv, all.subspan(37, all.size() - 37 - 32));
  if (!plain)
    return std::nullopt;
  auto read = knot::try_read<std::vector<exported_session>>(splice::bytes::chars(*plain));
  if (!read)
    return std::nullopt;
  return std::move(*read);
}

// Cross-signing's private keys: 32-byte Ed25519 seeds, unpadded base64.
struct cross_signing_secrets {
  std::string master;
  std::string self_signing;
  std::string user_signing;
  friend consteval auto json_schema(knot::type<cross_signing_secrets>) { return knot::schema<cross_signing_secrets>(); }
};
namespace detail {
struct pkey_free {
  void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};
// An Ed25519 key from its seed; its public half, and a signature made with it.
template <byte_range Seed>
[[nodiscard]] inline std::unique_ptr<EVP_PKEY, pkey_free> ed25519_of(Seed&& seed) {
  const auto exact = splice::bytes::exactly<32>(std::forward<Seed>(seed));
  if (!exact)
    return nullptr;
  return std::unique_ptr<EVP_PKEY, pkey_free>(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, exact->data(), exact->size()));
}
[[nodiscard]] inline std::optional<std::string> ed25519_public(std::string_view seed_b64) {
  const auto seed = from_base64(seed_b64);
  if (!seed || seed->size() != 32)
    return std::nullopt;
  const auto key = ed25519_of(*seed);
  std::array<std::uint8_t, 32> out{};
  std::size_t size = out.size();
  if (!key || EVP_PKEY_get_raw_public_key(key.get(), out.data(), &size) != 1 || size != out.size())
    return std::nullopt;
  return splice::bytes::base64_text(out);
}
[[nodiscard]] inline std::optional<std::string> ed25519_sign(std::string_view seed_b64, std::string_view message) {
  const auto seed = from_base64(seed_b64);
  if (!seed || seed->size() != 32)
    return std::nullopt;
  const auto key = ed25519_of(*seed);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  const auto bytes = splice::bytes::buffer_of(splice::bytes::of(message));  // Ed25519 signs the message whole
  std::array<std::uint8_t, 64> out{};
  std::size_t size = out.size();
  if (!key || !md || EVP_DigestSignInit(md.get(), nullptr, nullptr, nullptr, key.get()) != 1 ||
      EVP_DigestSign(md.get(), out.data(), &size, bytes.data(), bytes.size()) != 1 || size != out.size())
    return std::nullopt;
  return splice::bytes::base64_text(out);
}
}  // namespace detail
// Three new seeds: master, self-signing, user-signing.
[[nodiscard]] inline cross_signing_secrets new_cross_signing() {
  return {.master = splice::bytes::base64_text(mux::vault::vault::random(32)),
          .self_signing = splice::bytes::base64_text(mux::vault::vault::random(32)),
          .user_signing = splice::bytes::base64_text(mux::vault::vault::random(32))};
}
// A device's keys with signatures, as /keys/signatures/upload takes them.
struct signed_device_part {
  std::vector<std::string> algorithms;
  std::string device_id;
  std::map<std::string, std::string> keys;
  std::string user_id;
  signatures_t signatures;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<signed_device_part>) {
    return knot::schema<signed_device_part>().member<"rest">(knot::rest);
  }
};
// A cross-signing key with signatures, likewise.
template <class Key>
struct signed_key_part {
  std::string user_id;
  std::vector<typename Key::usage_item_t> usage;
  std::map<std::string, std::string> keys;
  signatures_t signatures;
  knot::raw rest;
  friend consteval auto json_schema(knot::type<signed_key_part>) {
    return knot::schema<signed_key_part>().template member<"rest">(knot::rest);
  }
};

// Secret storage (the spec's m.secret_storage.v1.aes-hmac-sha2): secrets
// kept in account data, each sealed under a key only the user holds -- the
// recovery key, written down as 48 base58 letters. With it, a new device
// takes the cross-signing keys back from the server it cannot read them on.
struct storage_key_info {
  std::string algorithm = "m.secret_storage.v1.aes-hmac-sha2";
  std::string iv;
  std::string mac;
  friend consteval auto json_schema(knot::type<storage_key_info>) { return knot::schema<storage_key_info>(); }
};
struct default_storage_key {
  std::string key;
  friend consteval auto json_schema(knot::type<default_storage_key>) { return knot::schema<default_storage_key>(); }
};
struct sealed_secret {
  std::string iv;
  std::string ciphertext;
  std::string mac;
  friend consteval auto json_schema(knot::type<sealed_secret>) { return knot::schema<sealed_secret>(); }
};
struct stored_secret {
  std::map<std::string, sealed_secret> encrypted;
  friend consteval auto json_schema(knot::type<stored_secret>) { return knot::schema<stored_secret>(); }
};
namespace detail {
// HKDF-SHA-256 with a zero salt, 64 bytes: AES key then HMAC key.
template <byte_range Key>
[[nodiscard]] inline std::array<std::uint8_t, 64> hkdf64(Key&& key, std::string_view info) {
  const std::array<std::uint8_t, 32> salt{};
  const auto prk = hmac_sha256(salt, key);
  const auto first = hmac_sha256(prk, splice::bytes::of(info), std::array<std::uint8_t, 1>{1});
  const auto second = hmac_sha256(prk, first, splice::bytes::of(info), std::array<std::uint8_t, 1>{2});
  std::array<std::uint8_t, 64> out{};
  std::ranges::copy(first, out.begin());
  std::ranges::copy(second, out.begin() + 32);
  return out;
}
inline constexpr std::string_view kBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
template <byte_range Bytes>
[[nodiscard]] inline std::string base58(Bytes&& bytes) {
  std::vector<std::uint8_t> digits;  // base 58, least significant first
  for (const std::uint8_t byte : bytes) {
    std::uint32_t carry = byte;
    for (std::uint8_t& digit : digits) {
      carry += std::uint32_t{digit} << 8;
      digit = static_cast<std::uint8_t>(carry % 58);
      carry /= 58;
    }
    for (; carry > 0; carry /= 58)
      digits.push_back(static_cast<std::uint8_t>(carry % 58));
  }
  const auto zeros = std::ranges::distance(bytes | std::views::take_while([](std::uint8_t b) { return b == 0; }));  // walked again
  return std::string(static_cast<std::size_t>(zeros), '1') +
         (digits | std::views::reverse | std::views::transform([](std::uint8_t d) { return kBase58[d]; }) |
          std::ranges::to<std::string>());
}
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> from_base58(std::string_view text) {
  std::vector<std::uint8_t> bytes;  // base 256, least significant first
  for (const char c : text) {
    const auto at = kBase58.find(c);
    if (at == std::string_view::npos)
      return std::nullopt;
    std::uint32_t carry = static_cast<std::uint32_t>(at);
    for (std::uint8_t& byte : bytes) {
      carry += std::uint32_t{byte} * 58;
      byte = static_cast<std::uint8_t>(carry & 0xFF);
      carry >>= 8;
    }
    for (; carry > 0; carry >>= 8)
      bytes.push_back(static_cast<std::uint8_t>(carry & 0xFF));
  }
  const auto zeros = std::ranges::distance(text | std::views::take_while([](char c) { return c == '1'; }));
  std::vector<std::uint8_t> out(static_cast<std::size_t>(zeros), 0);
  out.insert(out.end(), bytes.rbegin(), bytes.rend());
  return out;
}
// A secret sealed under a storage key, for its name.
template <byte_range Key>
[[nodiscard]] inline sealed_secret seal_secret(Key&& key, std::string_view name, std::string_view secret) {
  const auto keys = hkdf64(key, name);
  auto iv = mux::vault::vault::random(16);
  iv[8] &= 0x7F;
  const auto ciphertext = aes_ctr(std::span(keys).first(32), iv, splice::bytes::of(secret));
  if (!ciphertext)
    throw std::runtime_error("the secret could not be encrypted");
  const auto mac = hmac_sha256(std::span(keys).last(32), *ciphertext);
  return sealed_secret{.iv = splice::bytes::base64_text(iv), .ciphertext = splice::bytes::base64_text(*ciphertext), .mac = splice::bytes::base64_text(mac)};
}
template <byte_range Key>
[[nodiscard]] inline std::optional<std::string> open_secret(Key&& key, std::string_view name,
                                                            const sealed_secret& sealed) {
  const auto keys = hkdf64(key, name);
  const auto iv = from_base64(sealed.iv);
  const auto ciphertext = from_base64(sealed.ciphertext);
  const auto mac = from_base64(sealed.mac);
  if (!iv || !ciphertext || !mac || mac->size() != 32)
    return std::nullopt;
  const auto expected = hmac_sha256(std::span(keys).last(32), *ciphertext);
  if (CRYPTO_memcmp(expected.data(), mac->data(), 32) != 0)
    return std::nullopt;
  const auto plain = aes_ctr(std::span(keys).first(32), *iv, *ciphertext);
  if (!plain)
    return std::nullopt;
  return splice::bytes::text_of(*plain);  // the secret, kept as text
}
}  // namespace detail
// A new storage key: its 32 bytes, its ID, what account data says of it (an
// empty secret sealed, by which a key typed later is checked), and the
// recovery key the user writes down.
struct new_storage_key {
  std::vector<std::uint8_t> key;
  std::string id;
  storage_key_info info;
  std::string recovery;
};
template <detail::byte_range Key>
[[nodiscard]] inline std::string recovery_key_of(Key&& key) {
  std::vector<std::uint8_t> bytes{0x8B, 0x01};
  bytes.insert(bytes.end(), key.begin(), key.end());
  bytes.push_back(std::ranges::fold_left(bytes, std::uint8_t{0}, [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(a ^ b); }));
  const std::string plain = detail::base58(bytes);
  return splice::bytes::every(plain, 4, ' ') | std::ranges::to<std::string>();
}
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> key_of_recovery(std::string_view recovery) {
  const std::string plain = recovery | std::views::filter([](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; }) |
                            std::ranges::to<std::string>();
  const auto bytes = detail::from_base58(plain);
  if (!bytes || bytes->size() != 35 || (*bytes)[0] != 0x8B || (*bytes)[1] != 0x01)
    return std::nullopt;
  const auto parity = std::ranges::fold_left(*bytes, std::uint8_t{0}, [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>(a ^ b); });
  if (parity != 0)
    return std::nullopt;
  return std::vector<std::uint8_t>(bytes->begin() + 2, bytes->begin() + 34);
}
[[nodiscard]] inline new_storage_key make_storage_key() {
  auto key = mux::vault::vault::random(32);
  const std::string id = splice::bytes::base64_text(mux::vault::vault::random(24));
  const auto check = detail::seal_secret(key, "", std::string(32, '\0'));
  return new_storage_key{.key = key, .id = id, .info = {.iv = check.iv, .mac = check.mac}, .recovery = recovery_key_of(key)};
}
// Whether a key is the one account data describes: the empty secret it
// sealed opens to zeros under it.
template <detail::byte_range Key>
[[nodiscard]] inline bool is_storage_key(Key&& key, const storage_key_info& info) {
  const auto keys = detail::hkdf64(key, "");
  const auto zeros = detail::aes_ctr(std::span(keys).first(32), from_base64(info.iv).value_or(std::vector<std::uint8_t>{}),
                                     std::vector<std::uint8_t>(32, 0));
  const auto mac = from_base64(info.mac);
  if (!zeros || !mac || mac->size() != 32)
    return false;
  const auto expected = detail::hmac_sha256(std::span(keys).last(32), *zeros);
  return CRYPTO_memcmp(expected.data(), mac->data(), 32) == 0;
}

// Server-side key backup (the spec's m.megolm_backup.v1.curve25519-aes-sha2):
// each room key sealed to the backup's Curve25519 public key -- an
// ephemeral key, ECDH, HKDF-SHA-256 to an AES-256-CBC key, an HMAC key and
// an IV -- so that only whoever has the backup's private key (kept in
// secret storage, under the recovery key) reads it.
struct backup_session_data {
  std::string ephemeral;
  std::string ciphertext;
  std::string mac;
  friend consteval auto json_schema(knot::type<backup_session_data>) { return knot::schema<backup_session_data>(); }
};
struct backup_plaintext {
  std::string algorithm = "m.megolm.v1.aes-sha2";
  std::vector<std::string> forwarding_curve25519_key_chain;
  std::map<std::string, std::string> sender_claimed_keys;
  std::string sender_key;
  std::string session_key;
  friend consteval auto json_schema(knot::type<backup_plaintext>) { return knot::schema<backup_plaintext>(); }
};
struct backup_auth_data {
  std::string public_key;
  signatures_t signatures;
  friend consteval auto json_schema(knot::type<backup_auth_data>) { return knot::schema<backup_auth_data>(); }
};
struct backup_auth_signed_part {
  std::string public_key;
  friend consteval auto json_schema(knot::type<backup_auth_signed_part>) { return knot::schema<backup_auth_signed_part>(); }
};
namespace detail {
template <byte_range Secret>
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 32>> x25519_public(Secret&& secret_bytes) {
  const auto secret = splice::bytes::exactly<32>(std::forward<Secret>(secret_bytes));
  if (!secret)
    return std::nullopt;
  const std::unique_ptr<EVP_PKEY, pkey_free> key(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, secret->data(), secret->size()));
  std::array<std::uint8_t, 32> out{};
  std::size_t size = out.size();
  if (!key || EVP_PKEY_get_raw_public_key(key.get(), out.data(), &size) != 1 || size != out.size())
    return std::nullopt;
  return out;
}
template <byte_range Secret, byte_range Theirs>
[[nodiscard]] inline std::optional<std::array<std::uint8_t, 32>> x25519_shared(Secret&& secret_bytes, Theirs&& their_bytes) {
  const auto secret = splice::bytes::exactly<32>(std::forward<Secret>(secret_bytes));
  const auto theirs = splice::bytes::exactly<32>(std::forward<Theirs>(their_bytes));
  if (!secret || !theirs)
    return std::nullopt;
  const std::unique_ptr<EVP_PKEY, pkey_free> ours(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, secret->data(), secret->size()));
  const std::unique_ptr<EVP_PKEY, pkey_free> other(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, theirs->data(), theirs->size()));
  if (!ours || !other)
    return std::nullopt;
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(ours.get(), nullptr), &EVP_PKEY_CTX_free);
  std::array<std::uint8_t, 32> out{};
  std::size_t size = out.size();
  if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), other.get()) != 1 ||
      EVP_PKEY_derive(ctx.get(), out.data(), &size) != 1 || size != out.size())
    return std::nullopt;
  return out;
}
// HKDF-SHA-256, zero salt, empty info, 80 bytes: AES key, HMAC key, IV.
template <byte_range Shared>
[[nodiscard]] inline std::array<std::uint8_t, 80> hkdf80(Shared&& shared) {
  const std::array<std::uint8_t, 32> salt{};
  const auto prk = hmac_sha256(salt, shared);
  std::array<std::uint8_t, 80> out{};
  std::vector<std::uint8_t> previous;
  for (std::uint8_t round = 1, at = 0; at < out.size(); ++round) {
    const auto block = hmac_sha256(prk, previous, std::array<std::uint8_t, 1>{round});
    previous.assign(block.begin(), block.end());
    for (std::size_t i = 0; i < block.size() && at < out.size(); ++i, ++at)
      out[at] = block[i];
  }
  return out;
}
template <byte_range Key, byte_range Iv, byte_range In>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> aes_cbc(bool encrypt, Key&& key, Iv&& iv, In&& in) {
  return cipher(EVP_aes_256_cbc(), encrypt, std::forward<Key>(key), std::forward<Iv>(iv), std::forward<In>(in));
}
}  // namespace detail
// A backup key pair: its private half, and its public half as auth_data has it.
[[nodiscard]] inline std::pair<std::string, std::string> new_backup_key() {
  const auto secret = mux::vault::vault::random(32);
  const auto public_key = detail::x25519_public(secret);
  if (!public_key)
    throw std::runtime_error("the backup key could not be made");
  return {splice::bytes::base64_text(secret), splice::bytes::base64_text(*public_key)};
}
[[nodiscard]] inline std::optional<std::string> backup_public_of(std::string_view secret_b64) {
  const auto secret = from_base64(secret_b64);
  if (!secret || secret->size() != 32)
    return std::nullopt;
  const auto public_key = detail::x25519_public(*secret);
  return public_key ? std::optional<std::string>(splice::bytes::base64_text(*public_key)) : std::nullopt;
}
// A room key sealed to the backup's public key. Its MAC, as libolm made it
// and every client checks it, is of nothing: the first 8 bytes of
// HMAC-SHA-256 of an empty message.
[[nodiscard]] inline std::optional<backup_session_data> seal_backup(std::string_view public_b64, const backup_plaintext& plain) {
  const auto theirs = from_base64(public_b64);
  const auto ephemeral = mux::vault::vault::random(32);
  const auto ephemeral_public = detail::x25519_public(ephemeral);
  if (!theirs || theirs->size() != 32 || !ephemeral_public)
    return std::nullopt;
  const auto shared = detail::x25519_shared(ephemeral, *theirs);
  if (!shared)
    return std::nullopt;
  const auto keys = detail::hkdf80(*shared);
  const auto ciphertext =
      detail::aes_cbc(true, std::span(keys).first(32), std::span(keys).subspan(64, 16), splice::bytes::of(knot::to_json(plain)));
  if (!ciphertext)
    return std::nullopt;
  const auto mac = detail::hmac_sha256(std::span(keys).subspan(32, 32));
  return backup_session_data{.ephemeral = splice::bytes::base64_text(*ephemeral_public),
                             .ciphertext = splice::bytes::base64_text(*ciphertext),
                             .mac = splice::bytes::base64_text(std::span(mac).first(8))};
}
[[nodiscard]] inline std::optional<backup_plaintext> open_backup(std::string_view secret_b64, const backup_session_data& sealed) {
  const auto secret = from_base64(secret_b64);
  const auto ephemeral = from_base64(sealed.ephemeral);
  const auto ciphertext = from_base64(sealed.ciphertext);
  const auto mac = from_base64(sealed.mac);
  if (!secret || secret->size() != 32 || !ephemeral || ephemeral->size() != 32 || !ciphertext || !mac || mac->size() != 8)
    return std::nullopt;
  const auto shared = detail::x25519_shared(*secret, *ephemeral);
  if (!shared)
    return std::nullopt;
  const auto keys = detail::hkdf80(*shared);
  // Of nothing, as libolm and those after it write it; or of the
  // ciphertext, as the spec's text says.
  const auto of_nothing = detail::hmac_sha256(std::span(keys).subspan(32, 32));
  const auto of_ciphertext = detail::hmac_sha256(std::span(keys).subspan(32, 32), *ciphertext);
  if (CRYPTO_memcmp(of_nothing.data(), mac->data(), 8) != 0 && CRYPTO_memcmp(of_ciphertext.data(), mac->data(), 8) != 0)
    return std::nullopt;
  const auto plain = detail::aes_cbc(false, std::span(keys).first(32), std::span(keys).subspan(64, 16), *ciphertext);
  if (!plain)
    return std::nullopt;
  auto read = knot::try_read<backup_plaintext>(splice::bytes::chars(*plain));
  if (!read)
    return std::nullopt;
  return std::move(*read);
}
}  // namespace mux::proto::matrix::client::crypto
