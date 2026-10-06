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
// -- each sealed with its file's name as associated data: a sealed file, or
// a line of one, put in another's place does not open (review 4, M4). With
// the vault on, nothing unsealed is read (M2); with it off, nothing plain is
// written over what is sealed (M3).
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
import splice.bytes;

export namespace mux::vault {

// What vault.json keeps: never the key, only how to make it again and check.
struct header {
  std::int64_t version = 1;
  std::string salt;    // base64, 16 bytes
  std::int64_t memory_kib = 65536;
  std::int64_t passes = 3;
  std::int64_t lanes = 4;
  std::string check;   // base64 of an encrypted known text
  // A re-seal under way: turned on, its passphrase changed, or
  // being turned off -- what is on disk may be plain, or under the key
  // before, until it is done. Finished at the next start where it was cut
  // short; a file is never left that nothing can open.
  std::optional<bool> resealing;
  std::optional<bool> going_off;
  // The key before, sealed under this one, while a change is under way.
  std::optional<std::string> previous;
  friend consteval auto json_schema(knot::type<header>) { return knot::schema<header>(); }
};

using key_t = std::array<std::uint8_t, 32>;

namespace detail {
inline constexpr std::string_view kMagic = "MUXV1";
inline constexpr std::string_view kLinePrefix = "v1:";
inline constexpr std::string_view kCheckText = "mux vault check";
inline constexpr std::size_t kNonce = 12, kTag = 16;

// Bytes, as OpenSSL takes them: any range of them, made whole for its
// pointer only where it keeps one.
template <class Bytes>
concept byte_range = std::ranges::input_range<Bytes> && std::same_as<std::ranges::range_value_t<Bytes>, std::uint8_t>;

[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> from_base64(std::string_view text) {
  if (text.size() % 4 != 0)
    return std::nullopt;
  std::vector<std::uint8_t> out(3 * text.size() / 4 + 1);
  const auto in = spl::bytes::buffer_of(spl::bytes::of(text));  // EVP_DecodeBlock reads one block
  const int n = EVP_DecodeBlock(out.data(), in.data(), static_cast<int>(in.size()));
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

// AES-256-GCM: nonce | ciphertext | tag; `bound` the associated data -- the
// file it is for. What is sealed comes as any range of bytes, taken a piece
// at a time: a string seen as bytes, knot's lazy JSON -- never copied whole.
template <byte_range Plain>
[[nodiscard]] inline std::vector<std::uint8_t> seal(const key_t& key, Plain&& plain, std::string_view bound) {
  const auto nonce = random_bytes(kNonce);
  std::vector<std::uint8_t> out(nonce.begin(), nonce.end());
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
  if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data()) != 1)
    throw std::runtime_error("encryption failed");
  bool fine = true;
  spl::bytes::in_pieces(spl::bytes::of(bound), [&](std::span<const std::uint8_t> piece) {
    int len = 0;
    fine = fine && EVP_EncryptUpdate(ctx.get(), nullptr, &len, piece.data(), static_cast<int>(piece.size())) == 1;
  });
  spl::bytes::in_pieces(std::forward<Plain>(plain), [&](std::span<const std::uint8_t> piece) {
    const std::size_t at = out.size();
    out.resize(at + piece.size());
    int len = 0;
    fine = fine && EVP_EncryptUpdate(ctx.get(), out.data() + at, &len, piece.data(), static_cast<int>(piece.size())) == 1;
    out.resize(at + static_cast<std::size_t>(len));
  });
  int tail = 0;
  const std::size_t at = out.size();
  out.resize(at + kTag);
  if (!fine || EVP_EncryptFinal_ex(ctx.get(), out.data() + at, &tail) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTag), out.data() + at + tail) != 1)
    throw std::runtime_error("encryption failed");
  out.resize(at + static_cast<std::size_t>(tail) + kTag);
  return out;
}
// Opened: none where the key is another or the bytes were changed. The
// sealed bytes as any sized range that can be walked more than once (a
// file's text seen as bytes): its nonce, its tag, and what is between.
template <class Sealed>
  requires byte_range<Sealed> && std::ranges::forward_range<Sealed> && std::ranges::sized_range<Sealed>
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> open(const key_t& key, Sealed&& sealed, std::string_view bound) {
  const std::size_t total = std::ranges::size(sealed);
  if (total < kNonce + kTag)
    return std::nullopt;
  const auto nonce = spl::bytes::exactly<kNonce>(std::views::take(sealed, kNonce));
  auto tag = spl::bytes::exactly<kTag>(std::views::drop(sealed, total - kTag));
  if (!nonce || !tag)
    return std::nullopt;
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
  if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce->data()) != 1)
    return std::nullopt;
  bool fine = true;
  spl::bytes::in_pieces(spl::bytes::of(bound), [&](std::span<const std::uint8_t> piece) {
    int len = 0;
    fine = fine && EVP_DecryptUpdate(ctx.get(), nullptr, &len, piece.data(), static_cast<int>(piece.size())) == 1;
  });
  std::vector<std::uint8_t> out;
  out.reserve(total - kNonce - kTag);
  spl::bytes::in_pieces(std::views::take(std::views::drop(sealed, kNonce), total - kNonce - kTag),
                        [&](std::span<const std::uint8_t> piece) {
                          const std::size_t at = out.size();
                          out.resize(at + piece.size());
                          int len = 0;
                          fine = fine && EVP_DecryptUpdate(ctx.get(), out.data() + at, &len, piece.data(),
                                                           static_cast<int>(piece.size())) == 1;
                          out.resize(at + static_cast<std::size_t>(len));
                        });
  int tail = 0;
  const std::size_t at = out.size();
  out.resize(at + kTag);
  if (!fine || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTag), tag->data()) != 1 ||
      EVP_DecryptFinal_ex(ctx.get(), out.data() + at, &tail) != 1)
    return std::nullopt;  // a wrong key, or a file changed: not read
  out.resize(at + static_cast<std::size_t>(tail));
  return out;
}

// Argon2id, as OpenSSL 3.2 on has it. Its parameters by their names as
// OpenSSL reads them: an older core_names.h on the include path ahead of
// the OpenSSL linked has no macros for them.
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
      OSSL_PARAM_construct_uint32("memcost", &memory),
      OSSL_PARAM_construct_uint32("lanes", &lanes),
      OSSL_PARAM_construct_uint32("threads", &threads),
      OSSL_PARAM_construct_end()};
  key_t key{};
  const bool made = ctx && EVP_KDF_derive(ctx.get(), key.data(), key.size(), params) == 1;
  OPENSSL_cleanse(secret.data(), secret.size());
  if (!made)
    throw std::runtime_error("the key could not be made from the passphrase");
  return key;
}
// The associated data a file is sealed with: its name.
[[nodiscard]] inline std::string bound_of(const std::filesystem::path& path) { return path.filename().string(); }
inline constexpr std::string_view kCheckBound = "vault.json check";
inline constexpr std::string_view kPreviousBound = "vault.json previous key";

// Argon2's settings as vault.json may hold them: within these, or the file
// is not one -- a huge memory cost would make unlocking run out (review 4, L4).
[[nodiscard]] constexpr bool sane(const header& how) {
  return how.memory_kib >= 8192 && how.memory_kib <= 4 * 1024 * 1024 && how.passes >= 1 && how.passes <= 16 &&
         how.lanes >= 1 && how.lanes <= 16;
}
}  // namespace detail

// Read as well where it is not sealed, though the vault is on: only for
// sealing what was written before it was (turning it on).
struct migrating {};

// The vault of this run: off, or on with its key once unlocked.
class vault {
 public:
  [[nodiscard]] bool on() const {
    const std::scoped_lock held(lock_);
    return key_.has_value();
  }
  // Where its header is: on disk there, the vault is on, and locked until
  // its passphrase is given.
  void place(std::filesystem::path header_file) { header_file_ = std::move(header_file); }
  [[nodiscard]] bool exists() const {
    std::error_code ignored;
    return !header_file_.empty() && std::filesystem::exists(header_file_, ignored);
  }
  [[nodiscard]] bool locked() const {
    const std::scoped_lock held(lock_);
    return this->exists() && !key_;
  }

  // Unlocked with its passphrase: false where it is not the one.
  [[nodiscard]] bool unlock(std::string_view passphrase) {
    const std::scoped_lock held(lock_);
    const auto key = this->key_for(passphrase);
    if (!key)
      return false;
    key_ = *key;
    // A re-seal cut short: what it was doing, and the key before.
    if (const auto how = this->header_read()) {
      resealing_ = how->resealing.value_or(false);
      going_off_ = how->going_off.value_or(false);
      if (how->previous)
        if (const auto sealed = detail::from_base64(*how->previous))
          if (auto opened = detail::open(*key_, *sealed, detail::kPreviousBound); opened && opened->size() == 32) {
            key_t before{};
            std::ranges::copy(*opened, before.begin());
            previous_ = before;
          }
    }
    return true;
  }
  // A re-seal begun, its journal in the header first: turned on with a
  // passphrase; changed to another (the key before kept, sealed under the
  // new one); or being turned off. What is on disk is then written again
  // (write_all), and finish() ends it.
  void begin_encrypt(std::string_view passphrase) {
    const std::scoped_lock held(lock_);
    header how = this->fresh_header(passphrase);
    how.resealing = true;
    write_plain(header_file_, knot::to_json_string(how));
    resealing_ = true;
  }
  void begin_change(std::string_view passphrase) {
    const std::scoped_lock held(lock_);
    const auto before = key_;
    header how = this->fresh_header(passphrase);
    how.resealing = true;
    if (before)
      how.previous = spl::bytes::base64_padded_text(detail::seal(*key_, std::span(before->data(), before->size()), detail::kPreviousBound));
    write_plain(header_file_, knot::to_json_string(how));
    previous_ = before;
    resealing_ = true;
  }
  void begin_decrypt() {
    const std::scoped_lock held(lock_);
    auto how = this->header_read();
    if (!how)
      return;
    how->resealing = true;
    how->going_off = true;
    write_plain(header_file_, knot::to_json_string(*how));
    resealing_ = true;
    going_off_ = true;
  }
  // The re-seal done: the journal gone -- and, turned off, the header and
  // the key.
  void finish() {
    const std::scoped_lock held(lock_);
    if (going_off_) {
      this->remove();
    } else if (auto how = this->header_read()) {
      how->resealing.reset();
      how->previous.reset();
      write_plain(header_file_, knot::to_json_string(*how));
    }
    if (previous_)
      OPENSSL_cleanse(previous_->data(), previous_->size());
    previous_.reset();
    resealing_ = false;
    going_off_ = false;
  }
  [[nodiscard]] bool resealing() const {
    const std::scoped_lock held(lock_);
    return resealing_;
  }
  // Whether it is the passphrase, the vault left as it is: asked before it
  // is changed or turned off.
  [[nodiscard]] bool matches(std::string_view passphrase) const { return this->key_for(passphrase).has_value(); }
  // Whether the file at `path` is sealed while there is no header to open it
  // with: vault.json lost. Nothing is read or written then (review 4, M3).
  [[nodiscard]] bool sealed_without_header(const std::filesystem::path& path) const {
    return !this->exists() && !this->may_write(path);
  }

  // Turned on with a passphrase: its header written; what is on disk is
  // then sealed by the caller, through write_file.
  void create(std::string_view passphrase) {
    const std::scoped_lock held(lock_);
    write_plain(header_file_, knot::to_json_string(this->fresh_header(passphrase)));
  }
  // A header for a new passphrase -- its salt, its check -- the key it
  // makes taken as this vault's.
  [[nodiscard]] header fresh_header(std::string_view passphrase) {
    const std::scoped_lock held(lock_);
    header how;
    const auto salt = detail::random_bytes(16);
    how.salt = spl::bytes::base64_padded_text(salt);
    const key_t key = detail::derive(passphrase, salt, how);
    const std::string_view text = detail::kCheckText;
    how.check = spl::bytes::base64_padded_text(
        detail::seal(key, spl::bytes::of(text), detail::kCheckBound));
    key_ = key;
    return how;
  }
  [[nodiscard]] std::optional<header> header_read() const {
    const std::string text = spl::bytes::file_text(header_file_).value_or(std::string());
    auto how = knot::try_read<header>(text);
    if (!how)
      return std::nullopt;
    return std::move(*how);
  }
  // Turned off: what is on disk opened by the caller first, then the header
  // and the key gone.
  void remove() {
    const std::scoped_lock held(lock_);
    std::error_code ignored;
    std::filesystem::remove(header_file_, ignored);
    if (key_)
      OPENSSL_cleanse(key_->data(), key_->size());
    key_.reset();
    going_off_ = false;
  }

  // A whole file: read, opened where it is sealed (a sealed file with the
  // vault off, or with another key, is not read: nullopt).
  // With the vault on, a file not sealed is not read either: put there in
  // place of a sealed one, it would be believed (review 4, M2).
  [[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) const {
    return this->read_whole(path, false);
  }
  [[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path, migrating) const {
    return this->read_whole(path, true);
  }
  [[nodiscard]] std::optional<std::string> read_whole(const std::filesystem::path& path, bool plain_too) const {
    const std::scoped_lock held(lock_);
    auto text_read = spl::bytes::file_text(path);
    if (!text_read)
      return std::nullopt;
    std::string text = std::move(*text_read);
    if (!text.starts_with(detail::kMagic)) {
      if (key_ && !plain_too && !resealing_ && !text.empty())
        return std::nullopt;
      return text;
    }
    if (!key_)
      return std::nullopt;
    auto opened = this->opened_by_either(std::views::drop(spl::bytes::of(text), detail::kMagic.size()), detail::bound_of(path));
    if (!opened)
      return std::nullopt;
    return spl::bytes::text_of(*opened);
  }
  // A whole file written, through a temporary renamed over it: sealed where
  // the vault is on. Made the user's alone first where `secret`.
  [[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view text, bool secret = false) const {
    const std::scoped_lock held(lock_);
    if (!this->may_write(path))
      return false;
    return write_plain(path, this->encoded(path, text), secret);
  }
  // A whole file's bytes as they go on disk: sealed where the vault is on.
  [[nodiscard]] std::string encoded(const std::filesystem::path& path, std::string_view text) const {
    const std::scoped_lock held(lock_);
    if (!key_ || going_off_)
      return std::string(text);
    const auto sealed = detail::seal(*key_, spl::bytes::of(text), detail::bound_of(path));
    std::string out;
    out.reserve(detail::kMagic.size() + sealed.size());
    out.append(detail::kMagic);
    std::ranges::copy(spl::bytes::chars(sealed), std::back_inserter(out));
    return out;
  }
  // A line appended: sealed on its own where the vault is on.
  [[nodiscard]] bool append_line(const std::filesystem::path& path, std::string_view line) const {
    const std::scoped_lock held(lock_);
    if (!this->may_write(path))
      return false;
    std::error_code ignored;
    private_dir(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << this->line_of(line, path) << '\n';
    return static_cast<bool>(out.flush());
  }
  // A line for the file at `path`: sealed, bound to it, where the vault is on.
  [[nodiscard]] std::string line_of(std::string_view line, const std::filesystem::path& path) const {
    const std::scoped_lock held(lock_);
    if (!key_ || going_off_)
      return std::string(line);
    const auto sealed =
        detail::seal(*key_, spl::bytes::of(line), detail::bound_of(path));
    std::string out(detail::kLinePrefix);
    std::ranges::copy(spl::bytes::base64_padded(sealed), std::back_inserter(out));
    return out;
  }
  // A line read back from the file at `path`: opened where it is sealed;
  // nullopt where it cannot be, and, with the vault on, where it is not.
  [[nodiscard]] std::optional<std::string> open_line(std::string_view line, const std::filesystem::path& path) const {
    return this->open_one(line, path, false);
  }
  [[nodiscard]] std::optional<std::string> open_line(std::string_view line, const std::filesystem::path& path, migrating) const {
    return this->open_one(line, path, true);
  }
  [[nodiscard]] std::optional<std::string> open_one(std::string_view line, const std::filesystem::path& path,
                                                    bool plain_too) const {
    const std::scoped_lock held(lock_);
    if (!line.starts_with(detail::kLinePrefix)) {
      if (key_ && !plain_too && !resealing_)
        return std::nullopt;
      return std::string(line);
    }
    if (!key_)
      return std::nullopt;
    const auto bytes = detail::from_base64(line.substr(detail::kLinePrefix.size()));
    if (!bytes)
      return std::nullopt;
    auto opened = this->opened_by_either(*bytes, detail::bound_of(path));
    if (!opened)
      return std::nullopt;
    return std::string(opened->begin(), opened->end());
  }

  // Whether the file at `path` may be written now: not, with the vault off
  // (its header gone), where what is there is sealed -- plain text over it
  // would turn encryption at rest off without a word (review 4, M3).
  [[nodiscard]] bool may_write(const std::filesystem::path& path) const {
    const std::scoped_lock held(lock_);
    if (key_)
      return true;
    std::ifstream in(path, std::ios::binary);
    std::array<char, 5> start{};
    in.read(start.data(), start.size());
    const std::string_view head(start.data(), static_cast<std::size_t>(in.gcount()));
    return !head.starts_with(detail::kMagic) && !head.starts_with(detail::kLinePrefix);
  }
  // A directory of mux's: the user's alone, so that nothing in it -- a file
  // being written before it is narrowed, history with the vault off -- is
  // anyone else's to open (review 4, M5, M6).
  static void keep_private(const std::filesystem::path& dir) {
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ignored);
  }

  // Every file mux keeps through the vault: whole ones, and ones written a
  // line at a time.
  struct kept_files {
    std::vector<std::filesystem::path> whole;
    std::vector<std::filesystem::path> lines;
  };
  // What they hold, opened: to be sealed again, under another key or none.
  struct contents {
    std::vector<std::pair<std::filesystem::path, std::string>> whole;
    std::vector<std::pair<std::filesystem::path, std::vector<std::string>>> lines;
  };
  // All of them read, sealed or not: none where one there cannot be opened
  // -- then nothing is to be changed.
  [[nodiscard]] std::optional<contents> read_all(const kept_files& files) const {
    const std::scoped_lock held(lock_);
    contents out;
    std::error_code ignored;
    for (const auto& path : files.whole) {
      if (!std::filesystem::exists(path, ignored))
        continue;
      auto text = this->read_file(path, migrating{});
      if (!text)
        return std::nullopt;
      out.whole.emplace_back(path, std::move(*text));
    }
    for (const auto& path : files.lines) {
      auto text_read = spl::bytes::file_text(path);
      if (!text_read)
        continue;
      const std::string text = std::move(*text_read);
      auto opened = std::ranges::to<std::vector<std::optional<std::string>>>(std::views::transform(std::views::filter(std::views::transform(std::views::split(text, '\n'), [](auto&& line) { return std::string_view(line.begin(), line.end()); }), [](std::string_view line) { return !line.empty(); }), [&](std::string_view line) { return this->open_line(line, path, migrating{}); }));
      if (std::ranges::any_of(opened, [](const auto& one) { return !one.has_value(); }))
        return std::nullopt;
      out.lines.emplace_back(path, std::ranges::to<std::vector<std::string>>(std::views::transform(opened, [](auto& one) { return std::move(*one); })));
    }
    return out;
  }
  // All of it written again as the vault is now -- sealed under its key, or
  // plain where it is off -- each file put in its place whole.
  [[nodiscard]] bool write_all(const contents& all) const {
    const std::scoped_lock held(lock_);
    const bool whole_written = std::ranges::all_of(all.whole, [&](const auto& one) {
      return write_plain(one.first, this->encoded(one.first, one.second), true);
    });
    const bool lines_written = std::ranges::all_of(all.lines, [&](const auto& one) {
      const auto& [path, lines] = one;
      const std::string text = std::ranges::to<std::string>(std::views::join(std::views::transform(lines, [&](const std::string& line) { return this->line_of(line, path) + "\n"; })));
      return write_plain(path, text, true);
    });
    return whole_written && lines_written;
  }

  // Something done with every read and write of the vault waiting for it:
  // a re-seal, or a file put together and renamed into place.
  template <class Body>
  decltype(auto) exclusive(Body body) const {
    const std::scoped_lock held(lock_);
    return body();
  }

  // What the system gives for randomness: for keys made elsewhere (the E2EE
  // store's pickle key).
  [[nodiscard]] static std::vector<std::uint8_t> random(std::size_t n) { return detail::random_bytes(n); }

 private:
  // Sealed bytes opened by the key, or, mid-change, by the one before.
  template <class Sealed>
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> opened_by_either(Sealed&& sealed, std::string_view bound) const {
    if (auto opened = detail::open(*key_, sealed, bound))
      return opened;
    if (previous_)
      return detail::open(*previous_, sealed, bound);
    return std::nullopt;
  }
  std::optional<key_t> previous_;
  bool resealing_ = false;
  bool going_off_ = false;
  // The key a passphrase makes, where it is this vault's.
  [[nodiscard]] std::optional<key_t> key_for(std::string_view passphrase) const {
    const std::optional<header> how = this->header_read();
    if (!how || !detail::sane(*how))
      return std::nullopt;
    const auto salt = detail::from_base64(how->salt);
    const auto check = detail::from_base64(how->check);
    if (!salt || !check)
      return std::nullopt;
    const key_t key = detail::derive(passphrase, *salt, *how);
    const auto opened = detail::open(key, *check, detail::kCheckBound);
    if (!opened || std::string(opened->begin(), opened->end()) != detail::kCheckText)
      return std::nullopt;
    return key;
  }
  // A directory made: the user's alone where it is new.
  static void private_dir(const std::filesystem::path& dir) {
    std::error_code ignored;
    if (std::filesystem::create_directories(dir, ignored))
      std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ignored);
  }
  static bool write_plain(const std::filesystem::path& path, std::string_view bytes, bool secret = false) {
    std::error_code failed;
    private_dir(path.parent_path());
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
  // Every read and write of what is kept, and every change of the key, one
  // at a time, from whatever thread: a re-seal holds it from its first read
  // to its last write, so that nothing saved meanwhile -- the E2EE store, a
  // message's line -- is overwritten by the copy read before, or sealed
  // under a key about to go. Recursive: the members call each other.
  mutable std::recursive_mutex lock_;
};


}  // namespace mux::vault
