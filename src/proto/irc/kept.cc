// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.kept -- What an IRC account keeps of its own in the accounts
// file -- besides what every account keeps (mux.config's account_shared) --
// and what is asked of it by ADL there: its address, whether an address is
// an IRC one, its check, its form's draft.
//
// An IRC account's address is a URL: ircs://nick@irc.libera.chat[:6697] --
// TLS from the first byte -- or irc://nick@host[:6667], in the clear. It is
// read once (target_of) into where to connect and as whom.
export module mux.proto.irc.kept;

import std;
import knot;
import mux.proto.tags;

export namespace mux::proto::irc {

// Where an account connects, and as whom: its address, read.
struct target {
  bool tls = true;
  std::string nick;
  std::string host;
  std::uint16_t port = 6697;
  friend bool operator==(const target&, const target&) = default;
};
// ircs://nick@host[:port] or irc://nick@host[:port]; nothing where it is
// neither.
[[nodiscard]] inline std::optional<target> target_of(std::string_view address) {
  target out;
  if (address.starts_with("ircs://")) {
    address.remove_prefix(7);
  } else if (address.starts_with("irc://")) {
    address.remove_prefix(6);
    out.tls = false;
    out.port = 6667;
  } else {
    return std::nullopt;
  }
  address = address.substr(0, address.find('/'));
  const auto at = address.find('@');
  if (at == std::string_view::npos || at == 0) return std::nullopt;
  out.nick = std::string(address.substr(0, at));
  std::string_view host = address.substr(at + 1);
  if (const auto colon = host.rfind(':'); colon != std::string_view::npos) {
    unsigned port = 0;
    const auto digits = host.substr(colon + 1);
    const auto [end, wrong] = std::from_chars(digits.data(), digits.data() + digits.size(), port);
    if (wrong != std::errc{} || end != digits.data() + digits.size() || port == 0 || port > 65535) return std::nullopt;
    out.port = static_cast<std::uint16_t>(port);
    host = host.substr(0, colon);
  }
  if (host.empty()) return std::nullopt;
  out.host = std::string(host);
  return out;
}

struct kept {
  std::string address;  // ircs://nick@host[:port]
  // SASL: the account's password, and the account name where it is not the
  // nick. No password: no SASL.
  std::string password;
  std::optional<std::string> login;
  // The server's own password (PASS), where it asks one -- a bouncer's.
  std::optional<std::string> server_password;
  std::optional<std::string> username, realname;
  // The channels joined once connected.
  std::vector<std::string> channels;
  friend bool operator==(const kept&, const kept&) = default;
};
consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>().tag("irc"); }

// Its type, for the list of what accounts keep; the word the file says.
constexpr type_tag<kept> kept_type(const state&) { return {}; }
constexpr std::string_view protocol_word(const kept&) { return "irc"; }
constexpr std::string_view protocol_name(const kept&) { return "IRC"; }
// An irc:// or ircs:// address.
constexpr bool owns_address(const state&, std::string_view address) {
  return address.starts_with("ircs://") || address.starts_with("irc://");
}
inline kept kept_from(const state&, std::string address, std::string password) {
  return {.address = std::move(address), .password = std::move(password)};
}
[[nodiscard]] inline const std::string& address_of(const kept& one) noexcept { return one.address; }
// A server named by hand -- on the command line -- and its port: put into
// the address.
inline void server_given(kept& one, std::string server, std::optional<std::int64_t> port) {
  const auto was = target_of(one.address);
  const std::string nick = was ? was->nick : std::string("mux");
  const bool tls = was ? was->tls : true;
  one.address = std::format("{}://{}@{}{}", tls ? "ircs" : "irc", nick, server, port ? std::format(":{}", *port) : std::string());
}

inline std::optional<std::string> check(const kept& one) {
  if (one.address.empty())
    return "Type the address: ircs://nick@irc.libera.chat";
  if (one.address.find_first_of(" \t\r\n") != std::string::npos)
    return "An IRC address has no spaces";
  const auto where = target_of(one.address);
  if (!where)
    return "An IRC address is ircs://nick@server or irc://nick@server, with a port after a colon where it is not the usual";
  if (where->nick.find_first_of(" ,*?!@#:") != std::string::npos || where->nick.front() == '$')
    return "A nick has no spaces and none of , * ? ! @ # :";
  if (std::ranges::any_of(one.channels, [](const std::string& c) { return c.empty() || c.find_first_of(" ,\a") != std::string::npos; }))
    return "A channel's name has no spaces or commas";
  return std::nullopt;
}

// Raw form data, as typed: the channels as one line, separated by spaces or
// commas.
struct account_draft {
  std::string address, password, login, server_password, username, realname, channels;
  bool advanced = false;
  friend bool operator==(const account_draft&, const account_draft&) = default;
};
inline account_draft draft_of(const kept& saved) {
  return {saved.address,
          saved.password,
          saved.login.value_or(""),
          saved.server_password.value_or(""),
          saved.username.value_or(""),
          saved.realname.value_or(""),
          saved.channels | std::views::join_with(' ') | std::ranges::to<std::string>(),
          saved.login || saved.server_password || saved.username || saved.realname};
}
// The words of a line, separated by spaces or commas.
[[nodiscard]] inline std::vector<std::string> words_of(std::string_view line) {
  return line | std::views::transform([](char c) { return c == ',' ? ' ' : c; }) | std::ranges::to<std::string>() |
         std::views::split(' ') | std::views::filter([](auto part) { return !std::ranges::empty(part); }) |
         std::views::transform([](auto part) { return std::string(part.begin(), part.end()); }) |
         std::ranges::to<std::vector<std::string>>();
}
[[nodiscard]] inline std::expected<kept, std::string> account_from(const account_draft& draft) {
  const auto given = [](const std::string& s) { return s.empty() ? std::nullopt : std::optional(s); };
  kept out{.address = draft.address,
           .password = draft.password,
           .login = given(draft.login),
           .server_password = given(draft.server_password),
           .username = given(draft.username),
           .realname = given(draft.realname),
           .channels = words_of(draft.channels)};
  if (auto wrong = check(out)) return std::unexpected(*wrong);
  return out;
}

}  // namespace mux::proto::irc
