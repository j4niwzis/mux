// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.packs.types -- What the packs plugin names, and what it says it
// provides: Settings' link and an account's row to one's own pack, a room's
// settings' tab to its packs, the dialog they are edited in, and a part of
// the program. Light: mux.ui sees it.
export module mux.plugin.packs.types;

import std;
import mux.core;
import mux.composition;
import mux.plugin.room_settings.types;

export namespace mux::plugins::packs {

struct feature {
  static constexpr std::string_view name = "packs";
};

// Emojis & Stickers open: a room's packs, or one's own; and whether they
// may be changed.
struct shown {
  // Whose packs: the account's, and the room's where they are a room's --
  // what an answer from the server is matched by.
  std::optional<account_id> account;
  std::optional<std::string> room;
  bool editable = false;
  std::optional<std::string> key{};
  // As the account listed them; listed yet or not.
  std::vector<emote_pack> packs;
  bool listed = false;
  // A pack open -- a new one, not yet saved -- as it is being edited; each
  // opening its number, its fields made anew for it.
  std::optional<emote_pack> draft;
  bool new_pack = false;
  std::uint64_t opened = 0;
  // What is said over it: saving, saved, what went wrong.
  std::string note;
  friend bool operator==(const shown&, const shown&) = default;
};
// A room's settings' tab: its packs, opened in the dialog.
struct room_tab {};

// Asked of the program: one's own packs (Settings: the first account that
// has them; an account's page: that one's); a room's, from its settings --
// one of them opened where its key is said; a pack added to one's account;
// images chosen for the pack open; and the dialog's own: a pack opened (a
// new one where none is said), the list again, the one open named,
// attributed, its use flipped, one of its images renamed, its use flipped,
// taken out; it saved, or deleted.
struct open {};
struct open_account {
  std::string address;
};
struct open_room {
  std::optional<std::string> key{};
};
struct adopt {
  emote_pack pack;
  std::optional<account_id> by;
};
struct pick_images {};
struct pack_open {
  std::optional<std::size_t> at;
};
struct pack_back {};
struct pack_named {
  std::string text;
  bool attribution = false;
};
struct pack_use {
  bool emoji = true;
};
struct picture_renamed {
  std::string url, shortcode;
};
struct picture_use {
  std::string url;
  bool emoji = true;
};
struct picture_removed {
  std::string url;
};
struct pack_save {};
struct pack_delete {};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::account_rows, type_tag<feature>) { return true; }
// Its tab of a room's settings (the room_settings plugin's).
consteval bool provides(room_settings::contract::tabs, type_tag<feature>) { return true; }
constexpr composition::page_list<room_tab> room_tabs(room_settings::contract::tabs, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::packs
