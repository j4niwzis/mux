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
// And with the state too, where no account is there to have one -- an
// address alone -- the protocol's default state (state_before, state_of<P>{}):
//   owns_address(state, address)                 whether an address is its
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
// edit_rule(state) and its rule type, with an allows() of its own): any of
// one's own messages.
struct own_messages {};
[[nodiscard]] inline bool allows(own_messages, const conversation&, const message& one) { return own_text(one); }

// An account's settings pages of its protocol's own, beside the client's
// (Connection, Privacy, Chats, Proxy): their types, as account_pages(state)
// lists them. Each has its title, its icon and its page by overloads of its
// own, in the protocol's UI module (page_title, page_icon, page_type).
template <class... Pages>
struct account_page_list {};
// What a protocol's own UI asks of the program, beside what every
// protocol's does: its request types, as requests_of(state) lists them --
// the program's requests made of them all, each done by its overload of
// program_asked(app, request) in the protocol's program glue.
template <class... Requests>
struct request_list {};
// The dialogs a protocol has of its own, over the window: their types, as
// dialogs(state) lists them, each made by its dialog_type overload in the
// protocol's UI module and opened by the protocol's program glue.
template <class... Dialogs>
struct dialog_list {};

// What a protocol shows of its own beside the client's, made of the
// client's basic components: each said as data, drawn by the client's own
// nodes -- a line under a message, a badge after a chat's status, a banner
// over the composer. Which a protocol shows, and when, its overloads say
// (message_lines, header_badges, composer_banners), from its state, the chat
// and what the model knows: Matrix's banner only where the other person is
// not verified.
namespace part {
namespace tone {
struct plain {};   // as a note
struct accent {};  // as news
struct danger {};  // as a warning
}  // namespace tone
using tone_t = splice::variant<tone::plain, tone::accent, tone::danger>;
struct line {
  std::string text;
  tone_t tone = tone::plain{};
};
struct badge {
  std::string text;
  tone_t tone = tone::plain{};
  // By hand: splice's variant has no ==, and a tone is only which it is.
  friend bool operator==(const badge& a, const badge& b) { return a.text == b.text && a.tone.index() == b.tone.index(); }
};
// A banner, and its button where it has one: what it asks is one of the
// protocol's own requests (Asks), done by the protocol's program glue.
struct no_request {};
template <class Asks = no_request>
struct banner_of {
  std::string text;
  tone_t tone = tone::accent{};
  std::string button;  // none: no button
  std::optional<Asks> asks;
};
using banner = banner_of<>;
// What one may do in a chat, as its protocol says: write in it, let in
// those who knock.
struct chat_rights {
  bool post = true;
  bool invite = true;
};
// What one may do to someone in a chat: remove them, ban them.
struct person_rights {
  bool kick = false;
  bool ban = false;
};
// How a chat's messages are laid out, of the client's basic layouts:
// Telegram's bubbles, or lines as IRC clients show them -- full width, on
// the wallpaper, each sender's name over their run, theirs and one's own
// alike on the left.
namespace style {
struct bubbles {};
struct lines {};
}  // namespace style
using style_t = splice::variant<style::bubbles, style::lines>;
}  // namespace part

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
// What a contact never heard of is said to be: nothing -- where nothing
// says who is there, "offline" would be said of everyone.
inline std::string unheard_presence(const auto&) { return {}; }
// No pages of its own.
constexpr account_page_list<> account_pages(const auto&) { return {}; }
// No requests of its own.
constexpr request_list<> requests_of(const auto&) { return {}; }
// No dialogs of its own.
constexpr dialog_list<> dialogs(const auto&) { return {}; }
// Nothing shown of its own.
inline std::vector<part::line> message_lines(const auto&, const conversation&, const message&) { return {}; }
inline std::vector<part::badge> header_badges(const auto&, const conversation&, const auto&) { return {}; }
inline std::vector<part::badge> row_badges(const auto&, const conversation&) { return {}; }
// An address's local part, what someone is called before their name is
// known: the whole of it, unless the protocol says.
inline std::string local_part(const auto&, std::string_view address) { return std::string(address); }
// Bubbles.
inline part::style_t message_style(const auto&) { return part::style::bubbles{}; }
// Anything one may do in a chat; nothing to anyone in it.
inline part::chat_rights chat_rights(const auto&, const conversation&) { return {}; }
inline part::person_rights person_rights(const auto&, const conversation&, std::string_view) { return {}; }
// Nothing said of someone beside how they are.
inline std::vector<part::badge> person_badges(const auto&, const conversation*, const auto&, const account_id&, std::string_view) {
  return {};
}
// Any ID reads as a name, written in a message.
constexpr bool id_reads_as_name(const auto&, std::string_view) { return true; }
// No role said beside a sender's name.
inline std::string sender_role(const auto&, const conversation&, std::string_view) { return {}; }
// Any chat may be left.
inline bool can_leave(const auto&, const conversation&) { return true; }
// Whom a direct chat is with: the chat's own address.
inline std::string direct_contact(const auto&, const conversation& one) { return one.id.id; }
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
    return splice::visit([&](const auto& now) {
      using defaults::share_link;
      return share_link(now, address);
    }, speaks);
  }
} share_link{};

inline constexpr struct room_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, const conversation& chat) const {
    return splice::visit([&](const auto& now) {
      using defaults::room_link;
      return room_link(now, chat);
    }, speaks);
  }
} room_link{};

inline constexpr struct message_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, const conversation& chat, std::string_view id) const {
    return splice::visit([&](const auto& now) {
      using defaults::message_link;
      return message_link(now, chat, id);
    }, speaks);
  }
} message_link{};

inline constexpr struct person_link_t {
  template <class Speaks>
  std::optional<std::string> operator()(const Speaks& speaks, std::string_view who) const {
    return splice::visit([&](const auto& now) {
      using defaults::person_link;
      return person_link(now, who);
    }, speaks);
  }
} person_link{};

inline constexpr struct unheard_presence_t {
  template <class State>
  std::string operator()(const State& state) const {
    return splice::visit([](const auto& now) {
      using defaults::unheard_presence;
      return unheard_presence(now);
    }, state);
  }
} unheard_presence{};

// Whether a chat may be left: where it may not, its Leave is not shown.
inline constexpr struct can_leave_t {
  template <class State>
  bool operator()(const State& state, const conversation& chat) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::can_leave;
      return available(now) && can_leave(now, chat);
    }, state);
  }
} can_leave{};

// What a protocol shows of its own: under a message, after a chat's
// status in its header, over its composer.
inline constexpr struct message_lines_t {
  template <class State>
  std::vector<part::line> operator()(const State& state, const conversation& chat, const message& one) const {
    return splice::visit([&](const auto& now) {
      using defaults::message_lines;
      return message_lines(now, chat, one);
    }, state);
  }
} message_lines{};
inline constexpr struct header_badges_t {
  template <class State, class Model>
  std::vector<part::badge> operator()(const State& state, const conversation& chat, const Model& known) const {
    return splice::visit([&](const auto& now) {
      using defaults::header_badges;
      return header_badges(now, chat, known);
    }, state);
  }
} header_badges{};
// Whether an ID written in a message is shown as written, or by the name
// of what it names: a Matrix room's !id is no name for anyone.
inline constexpr struct id_reads_as_name_t {
  template <class State>
  bool operator()(const State& state, std::string_view id) const {
    return splice::visit([&](const auto& now) {
      using defaults::id_reads_as_name;
      return id_reads_as_name(now, id);
    }, state);
  }
} id_reads_as_name{};
// What one may do in a chat, and to someone in it.
inline constexpr struct chat_rights_t {
  template <class State>
  part::chat_rights operator()(const State& state, const conversation& chat) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::chat_rights;
      return available(now) ? chat_rights(now, chat) : part::chat_rights{false, false};
    }, state);
  }
} chat_rights{};
inline constexpr struct person_rights_t {
  template <class State>
  part::person_rights operator()(const State& state, const conversation& chat, std::string_view who) const {
    return splice::visit([&](const auto& now) {
      using defaults::available;
      using defaults::person_rights;
      return available(now) ? person_rights(now, chat, who) : part::person_rights{};
    }, state);
  }
} person_rights{};
// What is said of someone beside how they are, in a chat (none: their
// card): Matrix's word on their identity.
inline constexpr struct person_badges_t {
  template <class State, class Model>
  std::vector<part::badge> operator()(const State& state, const conversation* chat, const Model& known, const account_id& by,
                                      std::string_view who) const {
    return splice::visit([&](const auto& now) {
      using defaults::person_badges;
      return person_badges(now, chat, known, by, who);
    }, state);
  }
} person_badges{};
// The layout a protocol's chats show their messages in.
inline constexpr struct message_style_t {
  template <class State>
  part::style_t operator()(const State& state) const {
    return splice::visit([](const auto& now) {
      using defaults::message_style;
      return message_style(now);
    }, state);
  }
} message_style{};
// An address's local part (alice of alice@example.com, of @alice:x.org).
inline constexpr struct local_part_t {
  template <class State>
  std::string operator()(const State& state, std::string_view address) const {
    return splice::visit([&](const auto& now) {
      using defaults::local_part;
      return local_part(now, address);
    }, state);
  }
} local_part_of{};
// A sender's role in the chat, beside their name over their messages, as
// Telegram's "admin": its protocol's word for it, or none.
inline constexpr struct sender_role_t {
  template <class State>
  std::string operator()(const State& state, const conversation& chat, std::string_view who) const {
    return splice::visit([&](const auto& now) {
      using defaults::sender_role;
      return sender_role(now, chat, who);
    }, state);
  }
} sender_role{};
// And in the chat's row in the list, after what was said last.
inline constexpr struct row_badges_t {
  template <class State>
  std::vector<part::badge> operator()(const State& state, const conversation& chat) const {
    return splice::visit([&](const auto& now) {
      using defaults::row_badges;
      return row_badges(now, chat);
    }, state);
  }
} row_badges{};

// A protocol's own dialogs, asked of its state type.
template <class State>
constexpr auto dialogs_of(const State& state) {
  using defaults::dialogs;
  return dialogs(state);
}
// A protocol's own requests, asked of its state type.
template <class State>
constexpr auto protocol_requests_of(const State& state) {
  using defaults::requests_of;
  return requests_of(state);
}
// A protocol's own account pages, asked of its state type.
template <class State>
constexpr auto account_pages_of(const State& state) {
  using defaults::account_pages;
  return account_pages(state);
}

inline constexpr struct direct_contact_t {
  template <class State>
  std::string operator()(const State& state, const conversation& one) const {
    return splice::visit([&](const auto& now) {
      using defaults::direct_contact;
      return direct_contact(now, one);
    }, state);
  }
} direct_contact{};

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

// Whether an address is a protocol's: asked of its state -- a default one,
// as the protocol has to be found from the address.
inline constexpr struct owns_address_t {
  template <class State>
  bool operator()(const State& state, std::string_view address) const {
    using defaults::owns_address;
    return owns_address(state, address);
  }
} owns_address{};

}  // namespace mux::proto
