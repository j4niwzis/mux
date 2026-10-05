// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.kept -- What an XMPP account keeps of its own in the
// accounts file -- besides what every account keeps (mux.config's
// account_shared) -- and what is asked of it by ADL there: its address,
// whether an address is a JID, its check.
export module mux.proto.xmpp.kept;

import std;
import knot;
import mux.proto.tags;

export namespace mux::proto::xmpp {

// An answer to what the server asks to register (XEP-0077): its form's
// field, and what was typed in it -- or what the form held, where hidden.
struct registration_answer {
  std::string var;
  std::string value;
  friend bool operator==(const registration_answer&, const registration_answer&) = default;
};
consteval auto json_schema(knot::type<registration_answer>) { return knot::schema<registration_answer>(); }

struct kept {
  std::string address;  // user@domain
  std::string password;
  std::string resource = "mux";
  std::optional<std::string> host;
  std::optional<std::int64_t> port;
  bool plain_without_tls = false;
  // A new account, made on the server first, with these answers to what it
  // asks; both gone once it is made (registered).
  std::optional<bool> create;
  std::optional<std::vector<registration_answer>> answers;
  friend bool operator==(const kept&, const kept&) = default;
};
consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>().tag("xmpp"); }
// Accounts as files had them before: one list a protocol, under its name,
// every setting flat in each -- read twice by the client's config, once as
// what the account keeps (its own fields) and once as what every account
// keeps (the rest): an Entry of either.
template <class Entry>
struct legacy_accounts {
  std::optional<std::vector<Entry>> xmpp;
};
template <class Entry>
consteval auto json_schema(knot::type<legacy_accounts<Entry>>) {
  return knot::schema<legacy_accounts<Entry>>();
}
template <class Entry>
constexpr type_tag<legacy_accounts<Entry>> legacy_accounts_type(const kept&, type_tag<Entry>) {
  return {};
}
template <class Entry>
const std::optional<std::vector<Entry>>& legacy_entries(const legacy_accounts<Entry>& all) {
  return all.xmpp;
}
// A server named by hand -- on the command line -- and its port: where it
// connects, in place of what DNS says.
inline void server_given(kept& one, std::string server, std::optional<std::int64_t> port) {
  one.host = std::move(server);
  if (port)
    one.port = port;
}

// Its type, for the list of what accounts keep; the word the file says.
constexpr type_tag<kept> kept_type(const state&) { return {}; }
constexpr std::string_view protocol_word(const kept&) { return "xmpp"; }
constexpr std::string_view protocol_name(const kept&) { return "XMPP"; }
// A JID: anything a Matrix user ID is not (those begin with '@').
constexpr bool owns_address(const state&, std::string_view address) { return !address.empty() && !address.starts_with('@'); }
// Made on the server: signed in to from now on.
inline void forget_registration(kept& one) {
  one.create.reset();
  one.answers.reset();
}
inline kept kept_from(const state&, std::string address, std::string password) {
  return {.address = std::move(address), .password = std::move(password)};
}
[[nodiscard]] inline const std::string& address_of(const kept& one) noexcept { return one.address; }

inline std::optional<std::string> check(const kept& one) {
  const std::string_view address = one.address;
  if (address.empty())
    return "Type the address: user@example.com";
  if (address.find_first_of(" \t\r\n/") != std::string_view::npos)
    return "An XMPP address is user@domain, with no spaces";
  const auto at = address.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 == address.size() ||
      address.find('@', at + 1) != std::string_view::npos)
    return "An XMPP address is user@domain";
  if (one.resource.empty() || one.resource.find_first_of(" \t\r\n") != std::string::npos)
    return "The resource is a word with no spaces, such as mux";
  if (one.host && one.host->empty())
    return "Leave the host empty, or type one";
  if (one.port && (*one.port < 1 || *one.port > 65535))
    return "A port is a number from 1 to 65535";
  if (one.password.empty())
    return "Type the password";
  return std::nullopt;
}

}  // namespace mux::proto::xmpp
