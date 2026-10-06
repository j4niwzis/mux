// SPDX-License-Identifier: AGPL-3.0-only
// mux.protocols -- Every protocol's overloads, and the extension points that
// find them: what the rest of mux imports to ask a protocol anything. A
// protocol is added to the list in mux.proto.tags, and its module here.
export module mux.protocols;

import std;
import splice;
import mux.core;
import mux.config;
export import mux.proto.tags;
export import mux.proto.kept;
export import mux.proto;
export import mux.proto.xmpp;
export import mux.proto.matrix;
export import mux.proto.matrix.requests;

export namespace mux::proto {

// The protocol an address is of: the first in the list that owns it -- the
// list's first where none does.
template <class... Tags>
[[nodiscard]] protocol_t protocol_of(protocol_list<Tags...>, std::string_view address) {
  std::optional<protocol_t> found;
  (void)((owns_address(state_of<Tags>{}, address) ? (found = protocol_t{Tags{}}, true) : false) || ...);
  return found.value_or(protocol_t{});
}
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) { return protocol_of(protocols{}, address); }

// One of every protocol's own requests: what a banner's button asks.
template <class... Lists>
struct request_union;
template <class... Rs>
struct request_union<request_list<Rs...>> {
  using type = spl::variant<part::no_request, Rs...>;
};
template <class... As, class... Bs, class... Rest>
struct request_union<request_list<As...>, request_list<Bs...>, Rest...> : request_union<request_list<As..., Bs...>, Rest...> {};
template <class>
struct protocols_requests;
template <class... Tags>
struct protocols_requests<protocol_list<Tags...>> {
  using type = typename request_union<request_list<>, decltype(protocol_requests_of(state_of<Tags>{}))...>::type;
};
using any_request_t = typename protocols_requests<protocols>::type;
using any_banner = part::banner_of<any_request_t>;
// A protocol's request, as one of every protocol's: itself, or the one its
// own variant holds.
template <class R>
[[nodiscard]] std::optional<any_request_t> as_any(const std::optional<R>& one) {
  return one ? std::optional<any_request_t>(any_request_t{*one}) : std::nullopt;
}
template <class... Rs>
[[nodiscard]] std::optional<any_request_t> as_any(const std::optional<spl::variant<Rs...>>& one) {
  if (!one)
    return std::nullopt;
  return spl::visit([](const auto& each) { return std::optional<any_request_t>(any_request_t{each}); }, *one);
}

// No banners of its own, by default.
namespace banner_defaults {
inline std::vector<part::banner> composer_banners(const auto&, const conversation&, const auto&) { return {}; }
}  // namespace banner_defaults
// Over the composer, as its protocol says: its banners, each its button's
// request made one of every protocol's.
inline constexpr struct composer_banners_t {
  template <class State, class Model>
  std::vector<any_banner> operator()(const State& state, const conversation& chat, const Model& known) const {
    return spl::visit([&](const auto& now) {
      using banner_defaults::composer_banners;
      return std::ranges::to<std::vector>(std::views::transform(composer_banners(now, chat, known), [](auto one) {
               return any_banner{std::move(one.text), one.tone, std::move(one.button), as_any(one.asks)};
             }));
    }, state);
  }
} composer_banners{};

// Whether an address is media some protocol's server keeps -- a picture
// fetched through an account, as an avatar is: a custom emoji's, in a
// reaction or a message's HTML.
// No media of its own, by default: what its server keeps, by an address the
// client fetches through its account (Matrix's mxc://).
namespace media_defaults {
constexpr bool owns_media(const auto&, std::string_view) { return false; }
}  // namespace media_defaults
template <class... Tags>
[[nodiscard]] bool is_media(protocol_list<Tags...>, std::string_view uri) {
  using media_defaults::owns_media;
  return (owns_media(state_of<Tags>{}, uri) || ...);
}
[[nodiscard]] inline bool is_media(std::string_view uri) { return is_media(protocols{}, uri); }

// What a passphrase is asked for: the client's own, then every protocol's.
template <class... Lists>
struct passphrase_union;
template <class... Ps>
struct passphrase_union<passphrase_list<Ps...>> {
  using type = spl::variant<config::passphrase_for::unlock, config::passphrase_for::encrypt, config::passphrase_for::change,
                               config::passphrase_for::decrypt, Ps...>;
};
template <class... As, class... Bs, class... Rest>
struct passphrase_union<passphrase_list<As...>, passphrase_list<Bs...>, Rest...>
    : passphrase_union<passphrase_list<As..., Bs...>, Rest...> {};
template <class>
struct protocols_passphrases;
template <class... Tags>
struct protocols_passphrases<protocol_list<Tags...>> {
  using type = typename passphrase_union<passphrase_list<>, decltype(passphrases_of(state_of<Tags>{}))...>::type;
};
using passphrase_for_t = typename protocols_passphrases<protocols>::type;

// What a line typed in a chat is, as its protocol reads it before it is
// sent: one of its own requests (an IRC /join, a Telegram bot's /command),
// or nothing -- the text, sent as it is.
namespace command_defaults {
inline std::nullopt_t command_of(const auto&, const conversation_id&, std::string_view) { return std::nullopt; }
}  // namespace command_defaults
inline constexpr struct command_of_t {
  template <class State>
  std::optional<any_request_t> operator()(const State& state, const conversation_id& in, std::string_view typed) const {
    return spl::visit([&](const auto& now) {
      using command_defaults::command_of;
      return as_command(command_of(now, in, typed));
    }, state);
  }
  static std::optional<any_request_t> as_command(std::nullopt_t) { return std::nullopt; }
  template <class R>
  static std::optional<any_request_t> as_command(const std::optional<R>& asked) {
    return as_any(asked);
  }
} command_of{};

// Someone's card's buttons of a protocol's own: none by default.
using any_action = part::action_of<any_request_t>;
namespace action_defaults {
inline std::vector<part::action> person_actions(const auto&, const account_id&, std::string_view) { return {}; }
}  // namespace action_defaults
inline constexpr struct person_actions_t {
  template <class State>
  std::vector<any_action> operator()(const State& state, const account_id& by, std::string_view who) const {
    return spl::visit([&](const auto& now) {
      using action_defaults::person_actions;
      return std::ranges::to<std::vector>(std::views::transform(person_actions(now, by, who), [](auto one) {
               return any_action{std::move(one.label), as_any(one.asks)};
             }));
    }, state);
  }
} person_actions{};

}  // namespace mux::proto
