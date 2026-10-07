// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:base -- Settings, and reading values out of events.
export module mux.proto.matrix.client:base;

import std;
import mux.vault;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
export import loom.crypto;
import mux.config;
import mux.core;
import mux.http;
import mux.net;

export namespace mux::proto::matrix::client {

// End-to-end encryption: loom's (loom.crypto), its store kept in the
// program's vault -- sealed where local data is encrypted, the user's alone
// either way.
namespace crypto = loom::crypto;
struct vault_keeper {
  mux::vault::vault* vault = nullptr;
  [[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) const { return vault->read_file(path); }
  [[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view text, bool secret) const {
    return vault->write_file(path, text, secret);
  }
  template <class Plain>
  [[nodiscard]] std::vector<std::uint8_t> seal(const mux::vault::key_t& key, Plain&& plain, std::string_view bound) const {
    return mux::vault::detail::seal(key, std::forward<Plain>(plain), bound);
  }
  template <class Sealed>
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> open(const mux::vault::key_t& key, Sealed&& sealed, std::string_view bound) const {
    return mux::vault::detail::open(key, std::forward<Sealed>(sealed), bound);
  }
};
using olm_machine = crypto::olm_machine<vault_keeper>;

// Matrix's room rules and levels, as its protocol's state module has them
// (mux.proto.matrix.state): named here as before.
using proto::matrix::history_rule_t;
using proto::matrix::join_rule_t;
using proto::matrix::kCreatorPower;
using proto::matrix::power_need_t;
using proto::matrix::power_needs;
using proto::matrix::sends_state;
namespace join_rule = proto::matrix::join_rule;
namespace history_rule = proto::matrix::history_rule;
namespace power_need = proto::matrix::power_need;

// What an upload's name is in its URL: what is not unreserved, as %XX --
// each character a view, of itself where it is in `text` or of its escape
// in a table made once at compile time; nothing allocated until the caller
// appends it.
inline constexpr auto kPercentEscapes = [] {
  std::array<std::array<char, 3>, 256> out{};
  constexpr std::string_view digits = "0123456789ABCDEF";
  for (std::size_t byte = 0; byte < out.size(); ++byte)
    out[byte] = {'%', digits[byte >> 4], digits[byte & 0xF]};
  return out;
}();
[[nodiscard]] inline auto percent_encoded(std::string_view text) {
  return std::views::join(std::views::transform(text, [](const char& c) -> std::string_view {
           if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')
             return std::string_view(&c, 1);
           const auto& escape = kPercentEscapes[static_cast<unsigned char>(c)];
           return std::string_view(escape.data(), escape.size());
         }));
}

struct settings {
  std::string user_id;  // @user:example.org
  std::string password;
  // The client-server API's base URL, instead of what the server name's
  // .well-known says.
  std::optional<std::string> homeserver;
  std::string device_name = "mux";
  // A proxy to connect through, where there is one.
  std::optional<net::proxy> proxy;
  // The session kept from before: logged in with, rather than logging in
  // again -- a new device each time -- where it is still good.
  std::optional<std::string> access_token;
  std::optional<std::string> device_id;
  // How long a sync waits on the server for something to happen.
  std::chrono::milliseconds sync_timeout = std::chrono::seconds(30);
  // Where its end-to-end encryption is kept: this device's Olm account and
  // sessions, and the rooms' Megolm sessions. None: no encryption.
  std::filesystem::path crypto_store;
  // Room keys only to sessions verified -- cross-signed by their owner, or
  // by emoji here: Element's "Never send encrypted messages to unverified
  // sessions".
  bool only_verified = false;
  // Read mentions shared with the account's other sessions, and sealed.
  bool mentions_shared = false;
  bool mentions_sealed = false;
  // What is kept on disk is read and written through: the program's.
  mux::vault::vault* vault = nullptr;
  // A new account, registered before anything else (POST /register, its
  // interactive auth walked): the token a server registering by invitation
  // asks for, and whether the user agrees to the server's terms.
  bool create = false;
  std::optional<std::string> registration_token;
  bool accept_terms = false;
  // Signed in in the browser, on the server's own page (OAuth 2.0, the
  // authorization code with PKCE): the client the server knows mux as, and
  // the token that renews the session, where they are kept.
  bool oauth = false;
  std::optional<std::string> oauth_client_id;
  std::optional<std::string> refresh_token;
};

// A typed content as a request's body: its JSON text.
inline knot::raw as_body(const auto& content) { return knot::raw{knot::to_json_string(content)}; }

// A typed content's relation, written out for the encrypted envelope, which
// carries it in the clear: none where the content has none.
inline std::optional<knot::raw> relates_to_of(const auto& content) {
  if constexpr (requires { content.m_relates_to; })
    if (content.m_relates_to)
      return knot::raw{knot::to_json_string(*content.m_relates_to)};
  return std::nullopt;
}


}  // namespace mux::proto::matrix::client
