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
export import mux.proto.enabled.points;  // every protocol turned on's

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

// No banners of its own, by default.
namespace banner_defaults {
inline std::vector<part::banner> composer_banners(const auto&, const conversation&, const auto&) { return {}; }
}  // namespace banner_defaults
// Over the composer, as its protocol says: its banners, as they are shown --
// what each button asks is its protocol's, asked again where it is pressed
// (banner_asked).
inline constexpr struct composer_banners_t {
  template <class State, class Model>
  std::vector<part::banner> operator()(const State& state, const conversation& chat, const Model& known) const {
    return spl::visit([&](const auto& now) {
      using banner_defaults::composer_banners;
      return composer_banners(now, chat, known) | std::views::transform([](auto one) {
               return part::banner{std::move(one.text), one.tone, std::move(one.button), std::nullopt};
             }) | std::ranges::to<std::vector>();
    }, state);
  }
} composer_banners{};
// The first banner's button pressed: what it asks given to `asked`, in its
// protocol's own type -- the protocol decided here, in the visit.
inline constexpr struct banner_asked_t {
  template <class State, class Model, class Asked>
  void operator()(const State& state, const conversation& chat, const Model& known, const Asked& asked) const {
    spl::visit([&](const auto& now) {
      using banner_defaults::composer_banners;
      const auto banners = composer_banners(now, chat, known);
      if (!banners.empty() && banners.front().asks)
        asked(*banners.front().asks);
    }, state);
  }
} banner_asked{};

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
// or nothing -- the text, sent as it is. Each protocol's command_of, found
// by ADL in the visit of a chat's state; this, where it has none.
namespace command_defaults {
inline std::nullopt_t command_of(const auto&, const conversation_id&, std::string_view) { return std::nullopt; }
}  // namespace command_defaults

// Someone's card's buttons of a protocol's own: none by default. Shown by
// their labels -- those that ask something -- and pressed by where they are
// among them, what one asks given to `asked` in its protocol's own type.
namespace action_defaults {
inline std::vector<part::action> person_actions(const auto&, const account_id&, std::string_view) { return {}; }
}  // namespace action_defaults
inline constexpr struct person_actions_t {
  template <class State>
  std::vector<std::string> operator()(const State& state, const account_id& by, std::string_view who) const {
    return spl::visit([&](const auto& now) {
      using action_defaults::person_actions;
      return person_actions(now, by, who) | std::views::filter([](const auto& one) { return one.asks.has_value(); }) | std::views::transform([](const auto& one) { return one.label; }) | std::ranges::to<std::vector>();
    }, state);
  }
} person_actions{};
inline constexpr struct person_action_asked_t {
  template <class State, class Asked>
  void operator()(const State& state, const account_id& by, std::string_view who, std::size_t index, const Asked& asked) const {
    spl::visit([&](const auto& now) {
      using action_defaults::person_actions;
      auto offered = person_actions(now, by, who) | std::views::filter([](const auto& one) { return one.asks.has_value(); });
      auto at = std::ranges::next(std::ranges::begin(offered), static_cast<std::ptrdiff_t>(index), std::ranges::end(offered));
      if (at != std::ranges::end(offered))
        asked(*at->asks);
    }, state);
  }
} person_action_asked{};

// Explore, as a protocol has it: the hint in its search field; a room's
// address typed there that is joined at once, not looked up (XMPP's
// XEP-0045 rooms); and the link a room joined from its directory is opened
// by once it comes, in the protocol's own link type (as_some_link makes it
// any link). Asked of the protocol's default state, by its tag: only which
// protocol the account speaks is known where they are asked.
namespace directory_defaults {
constexpr std::string_view directory_hint(const auto&) { return "Name, topic, or address"; }
inline std::optional<std::string> directory_room(const auto&, std::string_view) { return std::nullopt; }
inline std::optional<std::monostate> directory_link(const auto&, std::string_view, const account_id&) { return std::nullopt; }
}  // namespace directory_defaults
inline constexpr struct directory_hint_t {
  std::string_view operator()(const protocol_t& which) const {
    return spl::visit([]<class Tag>(Tag) {
      using directory_defaults::directory_hint;
      return std::string_view(directory_hint(state_of<Tag>{}));
    }, which);
  }
} directory_hint{};
inline constexpr struct directory_room_t {
  std::optional<std::string> operator()(const protocol_t& which, std::string_view query) const {
    return spl::visit([&]<class Tag>(Tag) {
      using directory_defaults::directory_room;
      return directory_room(state_of<Tag>{}, query);
    }, which);
  }
} directory_room{};

}  // namespace mux::proto
