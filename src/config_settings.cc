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
  std::optional<bool> mentions_only;  // only @mentions and keywords notify
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
  return in.*spl::visit([](auto one) { return flag_member(one); }, flag);
}
// A chat's own choice of what notifies: everything, or what mentions the
// user -- muted chats are kept apart, as before.
struct chat_notify {
  std::string account;
  std::string conversation;
  std::optional<std::string> mode;  // "mentions" or "all", where chosen
  std::optional<bool> on;           // notifications on, where chosen (off: muted)
  std::optional<bool> name;
  std::optional<bool> text;
  std::optional<bool> sound;
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
  // Whether only mentions and keywords notify, and what a notification
  // shows: the sender's name, the message's text.
  std::optional<bool> notify_mentions;
  std::optional<bool> notify_name;
  std::optional<bool> notify_text;
  std::optional<std::string> proxy;
  std::optional<std::string> colour;
  std::optional<bool> strip;
  friend bool operator==(const account_shared&, const account_shared&) = default;
};
consteval auto json_schema(knot::type<account_shared>) { return knot::schema<account_shared>(); }

// Notifications, the same at every level -- every chat's, an account's, a
// space's, a chat's: on or off, of every message or of mentions alone, the
// sender's name and the text shown or not, a sound or not. Below the
// client's, each unsaid where it is as the level above.
struct notify_choices {
  std::optional<bool> on;
  std::optional<bool> mentions;
  std::optional<bool> name;
  std::optional<bool> text;
  std::optional<bool> sound;
  friend bool operator==(const notify_choices&, const notify_choices&) = default;
};
// Each setting: where a chat (or space) keeps it, where an account does,
// and the client's own, with what it is where nothing says.
namespace notify_setting {
struct on {
  static constexpr bool unsaid = true;
  static constexpr auto chat = &notify_choices::on;
  static constexpr auto account = &account_shared::notify;
  static bool of(const notification_settings& every) { return every.desktop; }
  static void set(notification_settings& every, bool now) { every.desktop = now; }
};
struct mentions {
  static constexpr bool unsaid = false;
  static constexpr auto chat = &notify_choices::mentions;
  static constexpr auto account = &account_shared::notify_mentions;
  static bool of(const notification_settings& every) { return every.mentions_only.value_or(false); }
  static void set(notification_settings& every, bool now) { every.mentions_only = now; }
};
struct name {
  static constexpr bool unsaid = true;
  static constexpr auto chat = &notify_choices::name;
  static constexpr auto account = &account_shared::notify_name;
  static bool of(const notification_settings& every) { return every.show_name; }
  static void set(notification_settings& every, bool now) { every.show_name = now; }
};
struct text {
  static constexpr bool unsaid = true;
  static constexpr auto chat = &notify_choices::text;
  static constexpr auto account = &account_shared::notify_text;
  static bool of(const notification_settings& every) { return every.show_text; }
  static void set(notification_settings& every, bool now) { every.show_text = now; }
};
struct sound {
  static constexpr bool unsaid = true;
  static constexpr auto chat = &notify_choices::sound;
  static constexpr auto account = &account_shared::notify_sound;
  static bool of(const notification_settings& every) { return every.sound; }
  static void set(notification_settings& every, bool now) { every.sound = now; }
};
}  // namespace notify_setting
using notify_setting_t =
    spl::variant<notify_setting::on, notify_setting::mentions, notify_setting::name, notify_setting::text, notify_setting::sound>;
// The client's, all said.
[[nodiscard]] inline notify_choices notify_choices_of(const notification_settings& every) {
  return {.on = every.desktop, .mentions = every.mentions_only.value_or(false), .name = every.show_name,
          .text = every.show_text, .sound = every.sound};
}
// An account's, as it keeps them.
[[nodiscard]] inline notify_choices notify_choices_of(const account_shared& one) {
  return {.on = one.notify, .mentions = one.notify_mentions, .name = one.notify_name, .text = one.notify_text,
          .sound = one.notify_sound};
}

// What each protocol keeps of its own: its kept type, found by ADL on its tag
// -- kept_type(state) -- for every protocol of the list.
template <class Tag>
using kept_of = typename decltype(kept_type(state_of<Tag>{}))::type;
template <class>
struct kept_list;
template <class... Tags>
struct kept_list<protocol_list<Tags...>> {
  using held = spl::variant<kept_of<Tags>...>;
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
