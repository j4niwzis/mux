// SPDX-License-Identifier: AGPL-3.0-only
// mux.config:config_settings -- What is kept of a proxy, the marks, the notifications, and an account -- the protocols' own, and every account's.
export module mux.config:config_settings;

import std;
import splice;
import knot;
import mux.vault;
import mux.proto.kept;
import :config_choices;

export namespace mux::config {
// A proxy, as a named profile of the program's list -- as Gajim keeps them
// -- that accounts choose by its name: SOCKS5 or HTTP CONNECT.
struct proxy_settings {
  std::string name;
  std::string kind = "socks5";  // "socks5" or "http"
  std::string host;
  std::int64_t port = 1080;
  std::optional<std::string> username;
  std::optional<std::string> password;
  // Who XMPP's SRV records are asked of, through it: a nameserver's address;
  // "off" for none (the domain itself on 5222, resolved by the proxy);
  // unset, the system's own nameserver, where the proxy can reach it.
  std::optional<std::string> srv_resolver;
  friend bool operator==(const proxy_settings&, const proxy_settings&) = default;
};

// Which kinds of room events show, each as chosen: nothing said is as the
// level under says -- a chat's its account's, an account's every one's.
struct room_event_kinds {
  std::optional<bool> joins, invites, names, avatars, room_name, topic, room_avatar, address, pins, permissions,
      access, encryption, other, unreadable, reactions, unreactions;
  friend bool operator==(const room_event_kinds&, const room_event_kinds&) = default;
};
consteval auto json_schema(knot::type<room_event_kinds>) { return knot::schema<room_event_kinds>(); }

// The mentions and reactions not yet seen, kept between runs, by chat.
struct kept_mark {
  std::string event;
  std::string target;
  std::int64_t at = 0;  // milliseconds since the epoch
  friend bool operator==(const kept_mark&, const kept_mark&) = default;
};
struct chat_marks {
  std::string account;
  std::string conversation;
  std::vector<kept_mark> mentions;
  std::vector<kept_mark> reactions;
  std::optional<std::vector<std::string>> seen;  // marks seen or gone to, by their event
  friend bool operator==(const chat_marks&, const chat_marks&) = default;
};
struct marks_file {
  std::vector<chat_marks> chats;
  friend bool operator==(const marks_file&, const marks_file&) = default;
};
consteval auto json_schema(knot::type<kept_mark>) { return knot::schema<kept_mark>(); }
consteval auto json_schema(knot::type<chat_marks>) { return knot::schema<chat_marks>(); }
consteval auto json_schema(knot::type<marks_file>) { return knot::schema<marks_file>(); }

// What notifies, and how, as Telegram Desktop's settings have it: a
// notification on the desktop, with the sender's name and the message's
// text or not, and a sound -- the chime, or a file -- by one backend.
struct notification_settings {
  bool desktop = true;
  bool show_name = true;
  bool show_text = true;
  bool sound = true;
  std::string backend = "native";
  std::optional<std::string> sound_file;
  // Woken by UnifiedPush, through the desktop's distributor (its D-Bus
  // specification) -- off unless chosen: only then is the bus asked, and
  // the servers given a pusher. The connection token, kept across runs, and
  // the endpoint the distributor gave for it.
  std::optional<bool> unified_push;
  std::optional<std::string> push_token;
  std::optional<std::string> push_endpoint;
  friend bool operator==(const notification_settings&, const notification_settings&) = default;
};
consteval auto json_schema(knot::type<notification_settings>) { return knot::schema<notification_settings>(); }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::desktop) { return &notification_settings::desktop; }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::show_name) { return &notification_settings::show_name; }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::show_text) { return &notification_settings::show_text; }
[[nodiscard]] constexpr bool notification_settings::* flag_member(notify_flag::sound) { return &notification_settings::sound; }
[[nodiscard]] inline bool& flag_in(notification_settings& in, const notify_flag_t& flag) {
  return in.*splice::visit([](auto one) { return flag_member(one); }, flag);
}
// A chat's own choice of what notifies: everything, or what mentions the
// user -- muted chats are kept apart, as before.
struct chat_notify {
  std::string account;
  std::string conversation;
  std::string mode;
  friend bool operator==(const chat_notify&, const chat_notify&) = default;
};
consteval auto json_schema(knot::type<chat_notify>) { return knot::schema<chat_notify>(); }

// An XMPP account: a JID and how to reach its server.
// What every saved account keeps, whatever its protocol: on or off, and the
// client's settings of it. What a protocol keeps of its own -- its address,
// its password, its server -- is its kept type, mux::proto::<protocol>::kept
// (through mux.proto.kept).
struct account_shared {
  bool enabled = true;
  std::optional<bool> read_receipts;
  std::optional<bool> send_typing;  // others' typing is always shown
  std::optional<bool> room_events;
  std::optional<room_event_kinds> room_event_kinds;
  std::optional<bool> show_receipts;
  std::optional<bool> link_previews;
  std::optional<bool> previews_direct;  // link previews fetched from the site itself
  std::optional<std::string> wallpaper;  // its chats' background, as word_of(wallpaper_t) says it
  std::optional<std::string> bubbles;    // its chats' bubbles, as word_of(bubble_look) says them
  std::optional<std::string> panels;     // its panels' look
  std::optional<bool> home_hides_spaced;  // Home without what spaces hold, but direct messages
  std::optional<bool> home_hides_direct;  // and without direct messages too, where it is so
  std::optional<std::int64_t> jump_search;
  std::optional<bool> notify;
  std::optional<bool> notify_sound;
  std::optional<std::string> proxy;
  std::optional<std::string> colour;
  std::optional<bool> strip;
  friend bool operator==(const account_shared&, const account_shared&) = default;
};
consteval auto json_schema(knot::type<account_shared>) { return knot::schema<account_shared>(); }

// What each protocol keeps of its own: its kept type, found by ADL on its tag
// -- kept_type(state) -- for every protocol of the list.
template <class Tag>
using kept_of = typename decltype(kept_type(state_of<Tag>{}))::type;
template <class>
struct kept_list;
template <class... Tags>
struct kept_list<protocol_list<Tags...>> {
  using held = splice::variant<kept_of<Tags>...>;
  // As the file has it, chosen by its "protocol": an account of a protocol
  // this build has not kept as it was, and written back so -- not lost.
  using saved = knot::tagged<"protocol", kept_of<Tags>..., knot::value>;
};
using kept_t = kept_list<protocols>::held;
using kept_saved_t = kept_list<protocols>::saved;

// One saved account: what its protocol keeps of its own, and what every
// account keeps.
struct account_t {
  kept_t own;
  account_shared shared;
  friend bool operator==(const account_t&, const account_t&) = default;
};

// An account as the file has it: {"protocol": "matrix", "own": {...},
// "shared": {...}}.
struct saved_account {
  std::string protocol;
  kept_saved_t own;
  account_shared shared;
  friend bool operator==(const saved_account&, const saved_account&) = default;
};
consteval auto json_schema(knot::type<saved_account>) { return knot::schema<saved_account>(); }



}  // namespace mux::config
