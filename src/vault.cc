// SPDX-License-Identifier: AGPL-3.0-only
// mux.vault -- What mux keeps on disk, encrypted where the user asked it to
// be: its settings (accounts' passwords and tokens among them), the
// messages, drafts and marks, the encryption store, the pictures kept.
//
// Off by default. Turned on, a passphrase gives a key by Argon2id (OpenSSL's,
// RFC 9106's recommended settings: 64 MiB, 3 passes, 4 lanes), its salt and
// parameters in vault.json beside the settings, with a check -- a known text
// encrypted -- that a wrong passphrase fails on. Each file is AES-256-GCM:
//
//   whole files:  "MUXV1" | nonce (12) | ciphertext | tag (16)
//   line files:   each line "v1:" base64(nonce | ciphertext | tag)
//
// -- a file appended to line by line (a chat's messages) stays one, each
// line on its own. Every write goes through here; with the vault off or
// unlocked as off, a file is written as it is.
module;
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
export module mux.vault;

import std;
import knot;

export namespace mux::vault {

// What vault.json keeps: never the key, only how to make it again and check.
struct header {
  std::int64_t version = 1;
  std::string salt;    // base64, 16 bytes
  std::int64_t memory_kib = 65536;
  std::int64_t passes = 3;
  std::int64_t lanes = 4;
  std::string check;   // base64 of an encrypted known text
  friend consteval auto json_schema(knot::type<header>) { return knot::schema<header>(); }
};

using key_t = std::array<std::uint8_t, 32>;

namespace detail {
inline constexpr std::string_view kMagic = "MUXV1";
inline constexpr std::string_view kLinePrefix = "v1:";
inline constexpr std::string_view kCheckText = "mux vault check";
inline constexpr std::size_t kNonce = 12, kTag = 16;

[[nodiscard]] inline std::string to_base64(std::span<const std::uint8_t> bytes) {
  std::string out(4 * ((bytes.size() + 2) / 3) + 1, '\0');
  const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), bytes.data(), static_cast<int>(bytes.size()));
  out.resize(static_cast<std::size_t>(n));
  return out;
}
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> from_base64(std::string_view text) {
  if (text.size() % 4 != 0)
    return std::nullopt;
  std::vector<std::uint8_t> out(3 * text.size() / 4 + 1);
  const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size()));
  if (n < 0)
    return std::nullopt;
  std::size_t size = static_cast<std::size_t>(n);
  // EVP_DecodeBlock counts the padding's bytes too.
  if (!text.empty() && text.back() == '=')
    size -= text.size() >= 2 && text[text.size() - 2] == '=' ? 2 : 1;
  out.resize(size);
  return out;
}

[[nodiscard]] inline std::vector<std::uint8_t> random_bytes(std::size_t n) {
  std::vector<std::uint8_t> out(n);
  if (RAND_bytes(out.data(), static_cast<int>(n)) != 1)
    throw std::runtime_error("no randomness from the system");
  return out;
}

// AES-256-GCM: nonce | ciphertext | tag.
[[nodiscard]] inline std::vector<std::uint8_t> seal(const key_t& key, std::span<const std::uint8_t> plain) {
  const auto nonce = random_bytes(kNonce);
  std::vector<std::uint8_t> out(kNonce + plain.size() + kTag);
  std::ranges::copy(nonce, out.begin());
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
  int len = 0;
  if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data()) != 1 ||
      EVP_EncryptUpdate(ctx.get(), out.data() + kNonce, &len, plain.data(), static_cast<int>(plain.size())) != 1)
    throw std::runtime_error("encryption failed");
  int tail = 0;
  if (EVP_EncryptFinal_ex(ctx.get(), out.data() + kNonce + len, &tail) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTag), out.data() + kNonce + plain.size()) != 1)
    throw std::runtime_error("encryption failed");
  return out;
}
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> open(const key_t& key, std::span<const std::uint8_t> sealed) {
  if (sealed.size() < kNonce + kTag)
    return std::nullopt;
  const std::size_t size = sealed.size() - kNonce - kTag;
  std::vector<std::uint8_t> out(size);
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
  int len = 0, tail = 0;
  std::array<std::uint8_t, kTag> tag{};
  std::ranges::copy(sealed.subspan(kNonce + size, kTag), tag.begin());
  if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), sealed.data()) != 1 ||
      EVP_DecryptUpdate(ctx.get(), out.data(), &len, sealed.data() + kNonce, static_cast<int>(size)) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTag), tag.data()) != 1 ||
      EVP_DecryptFinal_ex(ctx.get(), out.data() + len, &tail) != 1)
    return std::nullopt;  // a wrong key, or a file changed: not read
  return out;
}

// Argon2id, as OpenSSL 3.2 on has it.
[[nodiscard]] inline key_t derive(std::string_view passphrase, std::span<const std::uint8_t> salt, const header& how) {
  std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> kdf(EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr), &EVP_KDF_free);
  if (!kdf)
    throw std::runtime_error("this OpenSSL has no Argon2id");
  std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)> ctx(EVP_KDF_CTX_new(kdf.get()), &EVP_KDF_CTX_free);
  std::uint32_t memory = static_cast<std::uint32_t>(how.memory_kib), passes = static_cast<std::uint32_t>(how.passes),
                lanes = static_cast<std::uint32_t>(how.lanes), threads = 1;
  std::string secret(passphrase);
  std::vector<std::uint8_t> salted(salt.begin(), salt.end());
  const OSSL_PARAM params[] = {
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, secret.data(), secret.size()),
      OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salted.data(), salted.size()),
      OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &passes),
      OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memory),
      OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
      OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
      OSSL_PARAM_construct_end()};
  key_t key{};
  const bool made = ctx && EVP_KDF_derive(ctx.get(), key.data(), key.size(), params) == 1;
  OPENSSL_cleanse(secret.data(), secret.size());
  if (!made)
    throw std::runtime_error("the key could not be made from the passphrase");
  return key;
}
}  // namespace detail

// The vault of this run: off, or on with its key once unlocked.
class vault {
 public:
  [[nodiscard]] bool on() const { return key_.has_value(); }
  // Where its header is: on disk there, the vault is on, and locked until
  // its passphrase is given.
  void place(std::filesystem::path header_file) { header_file_ = std::move(header_file); }
  [[nodiscard]] bool exists() const {
    std::error_code ignored;
    return !header_file_.empty() && std::filesystem::exists(header_file_, ignored);
  }
  [[nodiscard]] bool locked() const { return this->exists() && !key_; }

  // Unlocked with its passphrase: false where it is not the one.
  [[nodiscard]] bool unlock(std::string_view passphrase) {
    std::ifstream in(header_file_, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto how = knot::try_read<header>(text);
    if (!how)
      return false;
    const auto salt = detail::from_base64(how->salt);
    const auto check = detail::from_base64(how->check);
    if (!salt || !check)
      return false;
    const key_t key = detail::derive(passphrase, *salt, *how);
    const auto opened = detail::open(key, *check);
    if (!opened || std::string(opened->begin(), opened->end()) != detail::kCheckText)
      return false;
    key_ = key;
    return true;
  }

  // Turned on with a passphrase: its header written; what is on disk is
  // then sealed by the caller, through write_file.
  void create(std::string_view passphrase) {
    header how;
    const auto salt = detail::random_bytes(16);
    how.salt = detail::to_base64(salt);
    const key_t key = detail::derive(passphrase, salt, how);
    const std::string_view text = detail::kCheckText;
    how.check = detail::to_base64(detail::seal(key, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())));
    write_plain(header_file_, knot::to_json_string(how));
    key_ = key;
  }
  // Turned off: what is on disk opened by the caller first, then the header
  // and the key gone.
  void remove() {
    std::error_code ignored;
    std::filesystem::remove(header_file_, ignored);
    if (key_)
      OPENSSL_cleanse(key_->data(), key_->size());
    key_.reset();
  }

  // A whole file: read, opened where it is sealed (a sealed file with the
  // vault off, or with another key, is not read: nullopt).
  [[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!text.starts_with(detail::kMagic))
      return text;
    if (!key_)
      return std::nullopt;
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()).subspan(detail::kMagic.size());
    auto opened = detail::open(*key_, bytes);
    if (!opened)
      return std::nullopt;
    return std::string(opened->begin(), opened->end());
  }
  // A whole file written, through a temporary renamed over it: sealed where
  // the vault is on. Made the user's alone first where `secret`.
  [[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view text, bool secret = false) const {
    std::string out;
    if (key_) {
      const auto sealed = detail::seal(*key_, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
      out.reserve(detail::kMagic.size() + sealed.size());
      out.append(detail::kMagic);
      out.append(reinterpret_cast<const char*>(sealed.data()), sealed.size());
    } else {
      out.assign(text);
    }
    return write_plain(path, out, secret);
  }
  // A line appended: sealed on its own where the vault is on.
  [[nodiscard]] bool append_line(const std::filesystem::path& path, std::string_view line) const {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << this->line_of(line) << '\n';
    return static_cast<bool>(out.flush());
  }
  [[nodiscard]] std::string line_of(std::string_view line) const {
    if (!key_)
      return std::string(line);
    const auto sealed = detail::seal(*key_, std::span(reinterpret_cast<const std::uint8_t*>(line.data()), line.size()));
    return std::string(detail::kLinePrefix) + detail::to_base64(sealed);
  }
  // A line read back: opened where it is sealed; nullopt where it cannot be.
  [[nodiscard]] std::optional<std::string> open_line(std::string_view line) const {
    if (!line.starts_with(detail::kLinePrefix))
      return std::string(line);
    if (!key_)
      return std::nullopt;
    const auto bytes = detail::from_base64(line.substr(detail::kLinePrefix.size()));
    if (!bytes)
      return std::nullopt;
    auto opened = detail::open(*key_, *bytes);
    if (!opened)
      return std::nullopt;
    return std::string(opened->begin(), opened->end());
  }

  // What the system gives for randomness: for keys made elsewhere (the E2EE
  // store's pickle key).
  [[nodiscard]] static std::vector<std::uint8_t> random(std::size_t n) { return detail::random_bytes(n); }

 private:
  static bool write_plain(const std::filesystem::path& path, std::string_view bytes, bool secret = false) {
    std::error_code failed;
    std::filesystem::create_directories(path.parent_path(), failed);
    const auto fresh = std::filesystem::path(path).concat(".new");
    { std::ofstream(fresh, std::ios::binary | std::ios::trunc); }
    if (secret)
      std::filesystem::permissions(fresh, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                   std::filesystem::perm_options::replace, failed);
    {
      std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
      out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      if (!out.flush())
        return false;
    }
    std::filesystem::rename(fresh, path, failed);
    return !failed;
  }

  std::filesystem::path header_file_;
  std::optional<key_t> key_;
};

// The one of this run: placed and unlocked at the start, before anything is read.
inline vault& the() {
  static vault one;
  return one;
}

}  // namespace mux::vault
