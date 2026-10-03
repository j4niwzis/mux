// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto -- What the client asks of a protocol, as extension points: a
// function object here for each, which calls the protocol's own overload --
// found by ADL in its tag's namespace -- or, where the protocol has none,
// the default below. Chosen by overload resolution, as std::ranges' are: a
// protocol's overload takes its tag itself, the default any type, and the
// one that fits better is the protocol's. So a protocol says only what it
// has; what it leaves out is the default, and nothing is written for it.
//
//   offers(speaks, feature::room_directory{})   what it has, by type
//   owns_address(tag, address)                  whether an address is its
//   share_link / room_link / message_link / person_link
//   can_pin(speaks, message id), may_delete(speaks, chat, outgoing)
//   may_edit(speaks, chat, message) -- through the rule edit_rule(tag) gives
//
// What a protocol gives may itself be a type with overloads of its own: an
// edit rule is a type of the protocol's, in mux::proto::<protocol>, with an
// allows() of its own -- the client's default where it gives none -- and
// what may be edited is asked of the rule.
export module mux.proto;

import std;
import splice;
import mux.core;

export namespace mux::proto {

// What a protocol may have, each a type: asked of it by offers().
namespace feature {
struct people_directory {};  // people looked up on its servers
struct room_directory {};    // rooms listed, searched and joined from a server's directory
struct room_creation {};     // rooms made
struct sticker_packs {};     // packs of stickers and emoji, a room's and an account's
struct history_context {};   // a window of history around a message, asked of the server
}  // namespace feature

// One's own, said rather than done (no room event), and still there: what
// an edit rule starts from.
[[nodiscard]] inline bool own_text(const message& one) { return one.outgoing && !one.service && !one.redacted; }
// The client's rule for edits, where a protocol gives none of its own (its
// edit_rule(tag) and its rule type, with an allows() of its own): any of
// one's own messages.
struct own_messages {};
[[nodiscard]] inline bool allows(own_messages, const conversation&, const message& one) { return own_text(one); }

}  // namespace mux::proto

// The defaults: what a protocol that says nothing of a thing comes to.
namespace mux::proto::defaults {
constexpr bool offers(const auto&, const auto&) { return false; }
constexpr bool owns_address(const auto&, std::string_view) { return false; }
inline std::optional<std::string> share_link(const auto&, std::string_view) { return std::nullopt; }
inline std::optional<std::string> room_link(const auto&, const conversation&) { return std::nullopt; }
inline std::optional<std::string> message_link(const auto&, const conversation&, std::string_view) { return std::nullopt; }
inline std::optional<std::string> person_link(const auto&, std::string_view) { return std::nullopt; }
constexpr bool can_pin(const auto&, std::string_view) { return false; }
// One's own messages, and no one else's: what every protocol allows.
inline bool may_delete(const auto&, const conversation&, bool outgoing) { return outgoing; }
// Any of one's own messages edited.
inline own_messages edit_rule(const auto&) { return {}; }
}  // namespace mux::proto::defaults

export namespace mux::proto {

// Each asked of the protocol a value speaks. The using-declaration names
// the default beside what ADL finds in the tag's namespace. Each a template
// on what it is asked of, though that is always a protocol_t: a function
// that is not one is compiled here, where no protocol's overloads can be
// seen, and every protocol got the default -- a template is instantiated
// where it is used, which imports mux.protocols and sees them all.
inline constexpr struct offers_t {
  template <class Speaks, class Feature>
  bool operator()(const Speaks& speaks, Feature wanted) const {
    return splice::visit([&](const auto& tag) {
      using defaults::offers;
      return offers(tag, wanted);
    }, speaks);
  }
} offers{};

inline constexpr struct share_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, std::string_view address) const {
    return splice::visit([&](const auto& tag) {
      using defaults::share_link;
      return share_link(tag, address);
    }, speaks);
  }
} share_link{};

inline constexpr struct room_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, const conversation& chat) const {
    return splice::visit([&](const auto& tag) {
      using defaults::room_link;
      return room_link(tag, chat);
    }, speaks);
  }
} room_link{};

inline constexpr struct message_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, const conversation& chat, std::string_view id) const {
    return splice::visit([&](const auto& tag) {
      using defaults::message_link;
      return message_link(tag, chat, id);
    }, speaks);
  }
} message_link{};

inline constexpr struct person_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, std::string_view who) const {
    return splice::visit([&](const auto& tag) {
      using defaults::person_link;
      return person_link(tag, who);
    }, speaks);
  }
} person_link{};

inline constexpr struct can_pin_t {
  template <class Speaks>
  bool operator()(const Speaks& speaks, std::string_view id) const {
    return splice::visit([&](const auto& tag) {
      using defaults::can_pin;
      return can_pin(tag, id);
    }, speaks);
  }
} can_pin{};

inline constexpr struct may_delete_t {
  template <class Speaks>
  bool operator()(const Speaks& speaks, const conversation& chat, bool outgoing) const {
    return splice::visit([&](const auto& tag) {
      using defaults::may_delete;
      return may_delete(tag, chat, outgoing);
    }, speaks);
  }
} may_delete{};

// Whether a message may be edited: asked of the rule the protocol gives.
inline constexpr struct may_edit_t {
  template <class Speaks>
  bool operator()(const Speaks& speaks, const conversation& chat, const message& one) const {
    return splice::visit([&](const auto& tag) {
      using defaults::edit_rule;
      return allows(edit_rule(tag), chat, one);
    }, speaks);
  }
} may_edit{};

// Whether an address is a protocol's: asked of the tag itself, as the
// protocol has to be found from it.
inline constexpr struct owns_address_t {
  template <class Tag>
  bool operator()(Tag tag, std::string_view address) const {
    using defaults::owns_address;
    return owns_address(tag, address);
  }
} owns_address{};

}  // namespace mux::proto
