// SPDX-License-Identifier: AGPL-3.0-only
// mux.config:config_limits -- A chat's own choices, the caches' limits, sending and history, and the settings file.
export module mux.config:config_limits;

import std;
import splice;
import knot;
import mux.vault;
import mux.proto.kept;
import :config_choices;
import :config_settings;
import :config_looks;

export namespace mux::config {
// A chat's own choice of whether what is done in it is shown.
struct room_events_choice {
  std::string account;       // the account's address
  std::string conversation;  // the chat's id in it
  std::optional<bool> show;  // all of them
  std::optional<room_event_kinds> kinds;  // each kind
  std::optional<bool> receipts;  // who has read up to where, as faces
  std::optional<bool> previews;  // a card for a message's first link
  std::optional<bool> previews_direct;  // fetched from the site itself, not the server
  std::optional<bool> typing;    // others told one is typing
  std::optional<std::int64_t> jump_search;  // events paged back looking for one; 0 no limit
  std::optional<std::string> wallpaper;  // its background, as word_of(wallpaper_t) says it
  std::optional<bool> forum;  // a space: shown as one chat, its rooms in it as topics
  std::optional<bool> hide_from_home;  // a space: its rooms not in Home
  std::optional<std::string> bubbles;    // its bubbles, as word_of(bubble_look) says them
  std::optional<std::string> panels;  // its panels' look, as word_of(bubble_look) says it
  friend bool operator==(const room_events_choice&, const room_events_choice&) = default;
};
consteval auto json_schema(knot::type<room_events_choice>) { return knot::schema<room_events_choice>(); }

// A chat listed in another account's list than its own -- a copy of its row
// there, or moved there -- the strip on its left as it chose: its colour,
// else its account's; shown, else as its account says.
struct chat_placement {
  std::string account;       // the chat's account, by its address
  std::string conversation;  // the chat's id in it
  std::string listed_in;     // the account whose list it is in, by its address
  bool moved = false;        // out of its own account's list
  std::optional<std::string> strip_colour;
  std::optional<bool> strip;
  friend bool operator==(const chat_placement&, const chat_placement&) = default;
};
consteval auto json_schema(knot::type<chat_placement>) { return knot::schema<chat_placement>(); }

// A chat muted: no notifications from it, its unread count in grey.
struct muted_chat {
  std::string account;       // the account's address
  std::string conversation;  // the chat's id in it
  friend bool operator==(const muted_chat&, const muted_chat&) = default;
};
// A sticker kept -- sent lately, or a favourite: what sending it again takes.
struct sticker_kept {
  std::string shortcode;
  std::string url;
  std::string body;
  std::optional<std::int64_t> w, h, size;
  std::optional<std::string> mimetype;
  friend bool operator==(const sticker_kept&, const sticker_kept&) = default;
  friend consteval auto json_schema(knot::type<sticker_kept>) { return knot::schema<sticker_kept>(); }
};

// The file: a list for each protocol, so each entry says what it is by where
// it is, and has only its own protocol's keys.
// How much is kept, in memory and on disk: what the Storage page sets.
struct cache_limits {
  std::int64_t messages_in_memory = 5000;
  std::int64_t messages_on_disk_mb = 512;
  std::int64_t pictures_in_memory_mb = 32;
  std::int64_t pictures_on_disk_mb = 512;
  // Deleted messages kept on disk, apart from the rest: none said is 256.
  std::optional<std::int64_t> deleted_on_disk_mb;
  friend bool operator==(const cache_limits&, const cache_limits&) = default;
};
consteval auto json_schema(knot::type<cache_limits>) { return knot::schema<cache_limits>(); }
// Which of them, as the page names them.
namespace limit {
struct messages_in_memory {
  friend bool operator==(messages_in_memory, messages_in_memory) = default;
};
struct messages_on_disk {
  friend bool operator==(messages_on_disk, messages_on_disk) = default;
};
struct pictures_in_memory {
  friend bool operator==(pictures_in_memory, pictures_in_memory) = default;
};
struct pictures_on_disk {
  friend bool operator==(pictures_on_disk, pictures_on_disk) = default;
};
struct deleted_on_disk {
  friend bool operator==(deleted_on_disk, deleted_on_disk) = default;
};
}  // namespace limit
using limit_t = spl::variant<limit::messages_in_memory, limit::messages_on_disk, limit::pictures_in_memory,
                             limit::pictures_on_disk, limit::deleted_on_disk>;
inline constexpr std::int64_t kDeletedOnDiskMb = 256;
[[nodiscard]] inline std::int64_t deleted_on_disk_of(const cache_limits& all) {
  return all.deleted_on_disk_mb.value_or(kDeletedOnDiskMb);
}
// A limit's number, and its bounds: what it can be halved or doubled to.
// Each limit's, by overloads; a limit_t's, by visiting them.
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::messages_in_memory) {
  return all.messages_in_memory;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::messages_on_disk) {
  return all.messages_on_disk_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::pictures_in_memory) {
  return all.pictures_in_memory_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::pictures_on_disk) {
  return all.pictures_on_disk_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, limit::deleted_on_disk) {
  if (!all.deleted_on_disk_mb)
    all.deleted_on_disk_mb = kDeletedOnDiskMb;
  return *all.deleted_on_disk_mb;
}
[[nodiscard]] inline std::int64_t& value_of(cache_limits& all, const limit_t& which) {
  return spl::visit([&](auto one) -> std::int64_t& { return value_of(all, one); }, which);
}
// Messages are counted, pictures weighed in MiB.
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::messages_in_memory) { return {250, 200000}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::messages_on_disk) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::pictures_in_memory) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::pictures_on_disk) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(limit::deleted_on_disk) { return {4, 65536}; }
[[nodiscard]] inline std::pair<std::int64_t, std::int64_t> bounds_of(const limit_t& which) {
  return spl::visit([](auto one) { return bounds_of(one); }, which);
}

// What is done to a picture dropped on the window before it is sent.
struct sending_settings {
  bool strip_metadata = true;  // its EXIF, XMP, text and the like cut out
  bool rename = true;          // named image.<its type>
  friend bool operator==(const sending_settings&, const sending_settings&) = default;
};
consteval auto json_schema(knot::type<sending_settings>) { return knot::schema<sending_settings>(); }

// What is kept of the history beyond what the servers keep.
struct history_settings {
  // A message deleted is shown where it was, all it said, marked; off, it
  // is gone from the chat. Either way it is kept on disk.
  bool show_deleted = false;
  // What is done in a room -- joins and leaves, a name or a picture changed,
  // events nothing here reads -- shown as lines of their own, or not. Kept
  // either way; an account's choice, and a room's own, come first.
  bool show_room_events = true;
  // Who has read up to where, as Element shows it: small faces under the
  // message each person read up to. Off unless chosen.
  bool show_receipts = false;
  // A card under a message for its first link, fetched through the
  // account's server. On unless turned off.
  bool link_previews = true;
  // Link previews fetched from the site itself, through the account's proxy,
  // instead of through its server: off -- the site then sees where
  // the request comes from.
  // Optional, as every field added to a kept file after it was first
  // written: knot requires the others, and a file saved before this one
  // came would no longer be read. Unset: off.
  std::optional<bool> previews_direct;
  // How many events a search for a message jumped to (a reply's, a link's)
  // pages back through before it gives up; 0 for no limit.
  std::int64_t jump_search = 5000;
  // And each kind of them, where chosen apart.
  std::optional<room_event_kinds> room_event_kinds;
  // Whether others are told one is typing (m.typing, XEP-0085's chat
  // states) -- never what: as every account's, until chosen there or in a
  // chat or its space.
  std::optional<bool> send_typing;  // unset: sent (and optional, as previews_direct)
  friend bool operator==(const history_settings&, const history_settings&) = default;
};
consteval auto json_schema(knot::type<history_settings>) { return knot::schema<history_settings>(); }

struct file {
  // The accounts, of every protocol, in order.
  std::optional<std::vector<saved_account>> accounts;
  // How much the window moves: "none", "reduced" (sections unfold, panels
  // just appear) or "full". Nothing said is full.
  std::optional<std::string> motion;
  // The account shown last, by its address: shown again at the next start.
  std::optional<std::string> last_account;
  // The emoji picked lately, newest first.
  std::optional<std::vector<std::string>> recent_emoji;
  // The stickers sent lately, newest first; and those made favourites.
  std::optional<std::vector<sticker_kept>> recent_stickers;
  std::optional<std::vector<sticker_kept>> favourite_stickers;
  std::optional<std::vector<muted_chat>> muted;
  // The chats listed in other accounts' lists than their own.
  std::optional<std::vector<chat_placement>> placements;
  // The chats that chose for themselves whether their room events show.
  std::optional<std::vector<room_events_choice>> room_events;
  // The proxy profiles accounts choose from.
  std::optional<std::vector<proxy_settings>> proxies;
  // The theme, "dark" or "light", and what draws the window, "opengl" or
  // "software". Nothing said is dark and OpenGL.
  std::optional<std::string> theme;
  std::optional<std::string> accent;
  // Every chat's background, as word_of(wallpaper_t) says it; none, the theme's.
  std::optional<std::string> wallpaper;
  // Every chat's bubbles, as word_of(bubble_look) says them; none, solid.
  std::optional<std::string> bubbles;
  std::optional<std::string> panels;  // its panels' look, as word_of(bubble_look) says it
  std::optional<bool> home_hides_spaced;  // Home without what spaces hold, but direct messages
  std::optional<bool> home_hides_direct;  // and without direct messages too, where it is so
  std::optional<std::string> renderer;
  // Only what changed repainted, into a frame kept between them.
  std::optional<bool> partial_redraw;
  // What each frame repainted, outlined: to see that only that is.
  std::optional<bool> flash_redraws;
  // Frames shown in step with the screen's refresh; off, as fast as drawn.
  std::optional<bool> vsync;
  // The window's opacity in percent (none, 100: opaque); and whether the
  // chat's background is behind the whole window, not only its messages.
  std::optional<int> window_opacity;
  std::optional<bool> wallpaper_behind;
  // Frosted popups and sheets blurring what is really under them, live.
  std::optional<bool> live_blur;
  // How much Frosted blurs what is behind, in percent (none, 30).
  // Frosted's blur, in percent of the most -- three times what it was
  // once (the old frost_blur's 100 is 33.3 here), any fraction of it.
  std::optional<double> frost;
  // The old setting, read where the new is not there yet: a third of it.
  std::optional<int> frost_blur;
  // Spaces in bars at all (none, yes); the bar along the top (none, yes);
  // and where each item is put, in order.
  std::optional<bool> spaces;
  std::optional<bool> top_bar;
  std::optional<std::vector<space_place>> space_places;
  // Frames a second, and the last frame's time, in the window's corner.
  std::optional<bool> show_fps;
  // The interface's scale, in percent of the display's own (none, 100): as
  // Telegram Desktop's "Interface scale".
  std::optional<int> interface_scale;
  std::optional<cache_limits> cache;
  std::optional<sending_settings> sending;
  std::optional<history_settings> history;
  std::optional<notification_settings> notifications;
  std::optional<std::vector<chat_notify>> chat_notify;
  friend bool operator==(const file&, const file&) = default;
};

consteval auto json_schema(knot::type<proxy_settings>) { return knot::schema<proxy_settings>(); }
consteval auto json_schema(knot::type<muted_chat>) { return knot::schema<muted_chat>(); }
consteval auto json_schema(knot::type<file>) { return knot::schema<file>(); }

}  // namespace mux::config
