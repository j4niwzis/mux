// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:names -- The names Matrix gives things, read into variants.
export module mux.proto.matrix.client:names;

import std;
import splice;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
export import :base;

export namespace mux::proto::matrix::client {

// What a request failed with: the server's error, or the network's.
// The names Matrix gives things, read into types where they come in: a
// message's msgtype, an event's type, a relation, a receipt, a room's
// type, a body's format. Each name is looked up once, in its *_of; what
// follows works on the variant.
// Each type says what it means as its members, read by visiting.
namespace msgtype {
struct image {
  static constexpr bool carries = true, picture = true, is_emote = false;
};
struct file {
  static constexpr bool carries = true, picture = false, is_emote = false;
};
struct video {
  static constexpr bool carries = true, picture = false, is_emote = false;
};
struct audio {
  static constexpr bool carries = true, picture = false, is_emote = false;
};
struct emote {
  static constexpr bool carries = false, picture = false, is_emote = true;
};
struct other {  // m.text, m.notice, and what is not known
  static constexpr bool carries = false, picture = false, is_emote = false;
};
struct gallery {  // MSC4274: several in one message, in its itemtypes
  static constexpr bool carries = false, picture = false, is_emote = false;
};
}  // namespace msgtype
using msgtype_t = splice::variant<msgtype::image, msgtype::file, msgtype::video, msgtype::audio, msgtype::emote,
                               msgtype::other, msgtype::gallery>;
namespace event_type {
struct encrypted {};           // m.room.encrypted
struct redaction {};           // m.room.redaction
struct receipt {};             // m.receipt
struct member {};              // m.room.member
struct room_name {};           // m.room.name
struct topic {};               // m.room.topic
struct room_avatar {};         // m.room.avatar
struct create {};              // m.room.create
struct power_levels {};        // m.room.power_levels
struct pinned {};              // m.room.pinned_events
struct join_rules {};          // m.room.join_rules
struct history_visibility {};  // m.room.history_visibility
struct canonical_alias {};     // m.room.canonical_alias
struct sticker {};             // m.sticker
struct message {};             // m.room.message, where its content was not read (redacted: emptied)
struct reaction {};            // m.reaction, likewise
struct other {};
}  // namespace event_type
using event_type_t =
    splice::variant<event_type::encrypted, event_type::redaction, event_type::receipt, event_type::member,
                 event_type::room_name, event_type::topic, event_type::room_avatar, event_type::create,
                 event_type::power_levels, event_type::pinned, event_type::join_rules,
                 event_type::history_visibility, event_type::canonical_alias, event_type::sticker, event_type::message,
                 event_type::reaction, event_type::other>;
// Where someone stands in a room, as an m.room.member says.
namespace membership {
struct join {
  static constexpr bool in = true;
};
struct leave {
  static constexpr bool in = false;
};
struct invite {
  static constexpr bool in = false;
};
struct ban {
  static constexpr bool in = false;
};
struct knock {
  static constexpr bool in = false;
};
struct other {
  static constexpr bool in = false;
};
}  // namespace membership
using membership_t = splice::variant<membership::join, membership::leave, membership::invite, membership::ban,
                                  membership::knock, membership::other>;
namespace relation {
struct replace {  // m.replace
  static constexpr bool edit = true;
};
struct other {
  static constexpr bool edit = false;
};
}  // namespace relation
using relation_t = splice::variant<relation::replace, relation::other>;
namespace room_type {
struct space {  // m.space
  static constexpr bool is_space = true;
};
struct other {
  static constexpr bool is_space = false;
};
}  // namespace room_type
using room_type_t = splice::variant<room_type::space, room_type::other>;
namespace body_format {
struct html {  // org.matrix.custom.html
  static constexpr bool html_given = true;
};
struct other {
  static constexpr bool html_given = false;
};
}  // namespace body_format
using body_format_t = splice::variant<body_format::html, body_format::other>;

// A name looked up in a table of the ones known; Other where it is not.
template <class Variant, class Other>
[[nodiscard]] Variant named(const std::unordered_map<std::string_view, Variant>& known,
                            std::optional<std::string_view> name) {
  if (!name)
    return Other{};
  const auto found = known.find(*name);
  return found == known.end() ? Variant{Other{}} : found->second;
}
[[nodiscard]] inline msgtype_t msgtype_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, msgtype_t> known = {
      {"m.image", msgtype::image{}}, {"m.file", msgtype::file{}},   {"m.video", msgtype::video{}},
      {"m.audio", msgtype::audio{}}, {"m.emote", msgtype::emote{}},
      {"dm.filament.gallery", msgtype::gallery{}}, {"m.gallery", msgtype::gallery{}},
  };
  return named<msgtype_t, msgtype::other>(known, name);
}
// Whether a message is a verification request (msgtype
// m.key.verification.request): read where the message comes in.
[[nodiscard]] inline bool verification_request_of(std::optional<std::string_view> msgtype) {
  return msgtype == std::optional<std::string_view>("m.key.verification.request");
}
// A verification step sent in a room, by its event type.
namespace verification_kind {
struct none {};
struct ready {};
struct start {};
struct accept {};
struct key {};
struct mac {};
struct cancel {};
struct done {};
}  // namespace verification_kind
using verification_kind_t =
    splice::variant<verification_kind::none, verification_kind::ready, verification_kind::start, verification_kind::accept,
                    verification_kind::key, verification_kind::mac, verification_kind::cancel, verification_kind::done>;
[[nodiscard]] inline verification_kind_t verification_kind_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, verification_kind_t> known = {
      {"m.key.verification.ready", verification_kind::ready{}},   {"m.key.verification.start", verification_kind::start{}},
      {"m.key.verification.accept", verification_kind::accept{}}, {"m.key.verification.key", verification_kind::key{}},
      {"m.key.verification.mac", verification_kind::mac{}},       {"m.key.verification.cancel", verification_kind::cancel{}},
      {"m.key.verification.done", verification_kind::done{}},
  };
  return named<verification_kind_t, verification_kind::none>(known, name);
}
[[nodiscard]] inline event_type_t event_type_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, event_type_t> known = {
      {"m.room.encrypted", event_type::encrypted{}},
      {"m.room.message", event_type::message{}},
      {"m.reaction", event_type::reaction{}},
      {"m.room.redaction", event_type::redaction{}},
      {"m.receipt", event_type::receipt{}},
      {"m.room.member", event_type::member{}},
      {"m.room.name", event_type::room_name{}},
      {"m.room.topic", event_type::topic{}},
      {"m.room.avatar", event_type::room_avatar{}},
      {"m.room.create", event_type::create{}},
      {"m.room.power_levels", event_type::power_levels{}},
      {"m.room.pinned_events", event_type::pinned{}},
      {"m.room.join_rules", event_type::join_rules{}},
      {"m.room.history_visibility", event_type::history_visibility{}},
      {"m.room.canonical_alias", event_type::canonical_alias{}},
      {"m.sticker", event_type::sticker{}},
  };
  return named<event_type_t, event_type::other>(known, name);
}
[[nodiscard]] inline membership_t membership_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, membership_t> known = {
      {"join", membership::join{}},     {"leave", membership::leave{}}, {"invite", membership::invite{}},
      {"ban", membership::ban{}},       {"knock", membership::knock{}},
  };
  return named<membership_t, membership::other>(known, name);
}
[[nodiscard]] inline relation_t relation_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, relation_t> known = {{"m.replace", relation::replace{}}};
  return named<relation_t, relation::other>(known, name);
}
[[nodiscard]] inline room_type_t room_type_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, room_type_t> known = {{"m.space", room_type::space{}}};
  return named<room_type_t, room_type::other>(known, name);
}
[[nodiscard]] inline body_format_t body_format_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, body_format_t> known = {
      {"org.matrix.custom.html", body_format::html{}}};
  return named<body_format_t, body_format::other>(known, name);
}

namespace state_type {
struct space_child {  // m.space.child
  static constexpr bool child = true;
  static constexpr bool emotes = false;
};
struct room_emotes {  // im.ponies.room_emotes: a pack of the room's custom emoji
  static constexpr bool child = false;
  static constexpr bool emotes = true;
};
struct other {
  static constexpr bool child = false;
  static constexpr bool emotes = false;
};
}  // namespace state_type
using state_type_t = splice::variant<state_type::space_child, state_type::room_emotes, state_type::other>;
[[nodiscard]] inline state_type_t state_type_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, state_type_t> known = {
      {"m.space.child", state_type::space_child{}}, {"im.ponies.room_emotes", state_type::room_emotes{}}};
  return named<state_type_t, state_type::other>(known, name);
}
// A room's join rule and history visibility, as their words say; and the
// words, as a room's state is written.
[[nodiscard]] inline join_rule_t join_rule_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, join_rule_t> known = {
      {"public", mux::proto::matrix::join_rule::open{}}, {"invite", mux::proto::matrix::join_rule::invite{}}, {"knock", mux::proto::matrix::join_rule::knock{}}};
  return named<join_rule_t, mux::proto::matrix::join_rule::other>(known, name);
}
[[nodiscard]] inline history_rule_t history_rule_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, history_rule_t> known = {
      {"shared", mux::proto::matrix::history_rule::shared{}},
      {"invited", mux::proto::matrix::history_rule::invited{}},
      {"joined", mux::proto::matrix::history_rule::joined{}},
      {"world_readable", mux::proto::matrix::history_rule::world_readable{}}};
  return named<history_rule_t, mux::proto::matrix::history_rule::shared>(known, name);
}
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::open) { return "public"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::invite) { return "invite"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::knock) { return "knock"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::join_rule::other) { return "invite"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::shared) { return "shared"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::invited) { return "invited"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::joined) { return "joined"; }
[[nodiscard]] constexpr std::string_view word_of(mux::proto::matrix::history_rule::world_readable) { return "world_readable"; }

// What an image of a pack may be used as (MSC2545's "usage").
// A server's errcode: the ones that say the session is gone, and the rest.
namespace errcode {
struct session_gone {  // M_UNKNOWN_TOKEN, M_FORBIDDEN
  static constexpr bool gone = true;
};
struct other {
  static constexpr bool gone = false;
};
}  // namespace errcode
using errcode_t = splice::variant<errcode::session_gone, errcode::other>;
[[nodiscard]] inline errcode_t errcode_of(std::optional<std::string_view> name) {
  static const std::unordered_map<std::string_view, errcode_t> known = {
      {"M_UNKNOWN_TOKEN", errcode::session_gone{}}, {"M_FORBIDDEN", errcode::session_gone{}}};
  return named<errcode_t, errcode::other>(known, name);
}

}  // namespace mux::proto::matrix::client
