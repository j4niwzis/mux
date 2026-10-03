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
  friend bool operator==(const kept&, const kept&) = default;
};
consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>().tag("matrix"); }

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
  if (one.password.empty())
    return "Type the password";
  return std::nullopt;
}

// An account edited: the same user on the same homeserver, with the same
// password, goes on with the device it has, rather than logging in as a
// new one at every Save.
inline void carry_over(kept& now, const kept& before) {
  if (before.user_id == now.user_id && before.homeserver == now.homeserver && before.password == now.password) {
    now.access_token = before.access_token;
    now.device_id = before.device_id;
  }
}
// The session the server gave at login, kept.
inline void take_session(kept& one, const auto& given) {
  one.access_token = given.access_token;
  one.device_id = given.device_id;
}

}  // namespace mux::proto::matrix
