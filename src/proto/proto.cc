// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto -- What the client asks of a protocol, as extension points: a
// function object here for each, which calls the protocol's own overload --
// found by ADL in its tag's namespace -- or, where the protocol has none,
// the default below. Chosen by overload resolution, as std::ranges' are: a
// protocol's overload takes its tag itself, the default any type, and the
// one that fits better is the protocol's. So a protocol says only what it
// has; what it leaves out is the default, and nothing is written for it.
//
// What may be done is asked of the account's state -- its protocol's state
// type, mux::proto::<protocol>::state: its stream, its server's facts --
// which every overload of the protocol is given, and available(state) first:
// false, nothing at all may be done.
//
//   available(state)                             anything at all
//   offers(state, feature::room_directory{})     what it has, by type
//   can_pin(state, message id), may_delete(state, chat, outgoing)
//   may_edit(state, chat, message) -- through the rule edit_rule(state) gives
//   can_page_back(state)                         older history asked of the server
//   can_upload(state)                            files sent: the paperclip, a drop, a paste
// And by the protocol alone, its tag:
//   owns_address(tag, address)                   whether an address is its
//   share_link / room_link / message_link / person_link
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

// What a protocol's account does beyond what every account does: a flag
// each, found by the program from the account type itself -- whether it has
// the call -- so that a thing is offered exactly where it is done, and
// nothing is listed by hand. All off: an account that does none of them.
struct account_ops {
  bool react = false;        // reactions sent
  bool forward = false;      // a message sent on, as it is
  bool threads = false;      // threads listed, read and answered in
  bool view_source = false;  // a message's source, and a room's state
  bool send_file = false;    // files and pictures
  bool send_sticker = false;
  bool typing = false;       // others told one is typing
  friend bool operator==(const account_ops&, const account_ops&) = default;
};

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
constexpr bool available(const auto&) { return true; }
constexpr bool can_page_back(const auto&) { return true; }
constexpr bool can_upload(const auto&) { return true; }
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
// Whether anything at all may be done, in the state an account is in.
inline constexpr struct available_t {
  bool operator()(const protocol_state_t& state) const {
    return splice::visit([](const auto& now) {
      using defaults::available;
      return available(now);
    }, state);
  }
} available{};

inline constexpr struct offers_t {
  template <class Feature>
  bool operator()(const protocol_state_t& state, Feature wanted) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::offers;
      return available(now) && offers(now, wanted);
    }, state);
  }
} offers{};

inline constexpr struct can_page_back_t {
  template <class State>
  bool operator()(const State& state) const {
    return splice::visit([](const auto& now) {
      using defaults::available;
      using defaults::can_page_back;
      return available(now) && can_page_back(now);
    }, state);
  }
} can_page_back{};

// Whether files may be sent now: a protocol forbids it by its state -- a
// server with no upload service, uploads turned off -- where its account
// sends them at all (account_ops::send_file).
inline constexpr struct can_upload_t {
  template <class State>
  bool operator()(const State& state) const {
    return splice::visit([](const auto& now) {
      using defaults::available;
      using defaults::can_upload;
      return available(now) && can_upload(now);
    }, state);
  }
} can_upload{};

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
  template <class State>
  bool operator()(const State& state, std::string_view id) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::can_pin;
      return available(now) && can_pin(now, id);
    }, state);
  }
} can_pin{};

inline constexpr struct may_delete_t {
  template <class State>
  bool operator()(const State& state, const conversation& chat, bool outgoing) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::may_delete;
      return available(now) && may_delete(now, chat, outgoing);
    }, state);
  }
} may_delete{};

// Whether a message may be edited: asked of the rule the protocol gives.
inline constexpr struct may_edit_t {
  template <class State>
  bool operator()(const State& state, const conversation& chat, const message& one) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::edit_rule;
      return available(now) && allows(edit_rule(now), chat, one);
    }, state);
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
