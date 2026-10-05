// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.kept -- What a Matrix account keeps of its own in the
// accounts file -- besides what every account keeps (mux.config's
// account_shared) -- and what is asked of it by ADL there.
export module mux.proto.matrix.kept;

import std;
import knot;
import mux.proto.tags;

export namespace mux::proto::matrix {

struct kept {
  std::string user_id;  // @user:server
  std::string password;
  std::optional<std::string> homeserver;
  std::string device_name = "mux";
  std::optional<bool> only_verified;  // room keys to verified sessions alone
  std::optional<std::string> access_token;
  std::optional<std::string> device_id;
  // A new account, to be registered on its homeserver before it is logged
  // in as: until the server gives it its session. With the token a server
  // that registers by invitation asks for, and the user's word that they
  // agree to the server's terms, where it has some.
  std::optional<bool> create;
  std::optional<std::string> registration_token;
  std::optional<bool> accept_terms;
  // Signed in in the browser, on the server's own page (OAuth 2.0): no
  // password kept; the client the server registered mux as, and the token
  // that renews the session.
  std::optional<bool> oauth;
  std::optional<std::string> oauth_client_id;
  std::optional<std::string> refresh_token;
  friend bool operator==(const kept&, const kept&) = default;
};
consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>().tag("matrix"); }
// Accounts as files had them before: one list a protocol, under its name,
// every setting flat in each -- read twice by the client's config, once as
// what the account keeps (its own fields) and once as what every account
// keeps (the rest): an Entry of either.
template <class Entry>
struct legacy_accounts {
  std::optional<std::vector<Entry>> matrix;
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
  return all.matrix;
}
// A server named by hand: the homeserver, in place of what .well-known says.
inline void server_given(kept& one, std::string server, std::optional<std::int64_t>) { one.homeserver = std::move(server); }

constexpr type_tag<kept> kept_type(const state&) { return {}; }
constexpr std::string_view protocol_word(const kept&) { return "matrix"; }
constexpr std::string_view protocol_name(const kept&) { return "Matrix"; }
// A user ID: @localpart:server.
constexpr bool owns_address(const state&, std::string_view address) { return address.starts_with('@'); }
inline kept kept_from(const state&, std::string address, std::string password) {
  return {.user_id = std::move(address), .password = std::move(password)};
}
[[nodiscard]] inline const std::string& address_of(const kept& one) noexcept { return one.user_id; }
// Whether its room keys go to verified sessions alone: Matrix's alone.
[[nodiscard]] inline std::optional<bool>* only_verified_in(kept& one) { return &one.only_verified; }
[[nodiscard]] inline const std::optional<bool>* only_verified_in(const kept& one) { return &one.only_verified; }

inline std::optional<std::string> check(const kept& one) {
  const std::string_view user = one.user_id;
  if (user.empty())
    return "Type the user ID: @user:example.org";
  if (user.find_first_of(" \t\r\n") != std::string_view::npos)
    return "A user ID has no spaces in it";
  const auto colon = user.find(':');
  if (!user.starts_with('@') || colon == std::string_view::npos || colon == 1 || colon + 1 == user.size())
    return "A Matrix user ID is @user:server";
  if (one.homeserver && !one.homeserver->starts_with("https://") && !one.homeserver->starts_with("http://"))
    return "The homeserver is a URL: https://matrix.example.org";
  if (one.device_name.empty())
    return "Name this device, such as mux";
  if (one.password.empty() && !one.oauth.value_or(false))
    return "Type the password, or sign in in the browser";
  return std::nullopt;
}

// An account edited: the same user on the same homeserver, with the same
// password, goes on with the device it has, rather than logging in as a
// new one at every Save.
inline void carry_over(kept& now, const kept& before) {
  const bool same_way = before.oauth.value_or(false) == now.oauth.value_or(false) &&
                        (now.oauth.value_or(false) || before.password == now.password);
  if (before.user_id == now.user_id && before.homeserver == now.homeserver && same_way) {
    now.access_token = before.access_token;
    now.device_id = before.device_id;
    now.refresh_token = before.refresh_token;
    now.oauth_client_id = before.oauth_client_id;
  }
}
// The session the server gave at login, kept.
inline void take_session(kept& one, const auto& given) {
  one.access_token = given.access_token;
  one.device_id = given.device_id;
  if (given.refresh_token)
    one.refresh_token = given.refresh_token;
  if (given.oauth_client_id)
    one.oauth_client_id = given.oauth_client_id;
  // Registered: logged in as from now on.
  one.create.reset();
  one.registration_token.reset();
}

}  // namespace mux::proto::matrix
