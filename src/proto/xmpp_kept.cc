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

struct kept {
  std::string address;  // user@domain
  std::string password;
  std::string resource = "mux";
  std::optional<std::string> host;
  std::optional<std::int64_t> port;
  bool plain_without_tls = false;
  friend bool operator==(const kept&, const kept&) = default;
};
consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>().tag("xmpp"); }

// Its type, for the list of what accounts keep; the word the file says.
constexpr std::type_identity<kept> kept_type(tag) { return {}; }
constexpr std::string_view protocol_word(const kept&) { return "xmpp"; }
constexpr std::string_view protocol_name(const kept&) { return "XMPP"; }
// A JID: anything a Matrix user ID is not (those begin with '@').
constexpr bool owns_address(tag, std::string_view address) { return !address.empty() && !address.starts_with('@'); }
inline kept kept_from(tag, std::string address, std::string password) {
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
