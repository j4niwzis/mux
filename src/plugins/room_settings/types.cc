// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_settings.types -- What the room settings plugin names, and
// what it says it provides: a room's settings, a dialog of the window's, and
// a part of the program; and the extension points it owns -- its own
// contracts, which other plugins provide (room_admin, packs, devtools).
// Light: mux.ui sees it.
export module mux.plugin.room_settings.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::room_settings {

struct feature {
  static constexpr std::string_view name = "room settings";
};

// What a room's settings let other plugins add, said as composition's
// contracts are (provides(c, type_tag<feature>) -> true) and asked by ADL.
namespace contract {
// Tabs beside the client's own (General, Notifications, Appearance):
//   room_tabs(c, f) -> composition::page_list<its tabs...>. Each tab a type
//   of its own, with tab_title(tab) -> std::string_view, tab_icon(tab),
//   make_manage_page(tab, palette, facts) -> its page; and where not every
//   room has it, tab_visible(tab, facts). One whose page holds the client's
//   General under its own says hosts_general(tab) -> true. An editor open
//   on its page: manage_editor_header(tab, kept) -> bool, a way back shown
//   over it, and manage_close_editor(tab, kept&) -> bool, closed by Back or
//   Esc first. All found by ADL; those taking only its types and what it
//   keeps (hosts_general, the editor's, tab_title) declared with its types.
struct tabs {
  static constexpr std::string_view name = "a room's settings' tabs";
};
// What it keeps while a room's settings are open, beside the facts:
//   kept_parts(c, f) -> composition::page_list<its parts...>, one of them
//   held at a time (facts.manage); avatar_sending(part&), a room's avatar on
//   its way, by ADL.
struct kept {
  static constexpr std::string_view name = "what a room's settings keep";
};
// The settings as they open for a room:
//   room_opened(c, f, facts&, const conversation&) -- what it fills of the
//   facts (a template on them, with its types): its tab up first, what it
//   keeps, the levels it knows.
struct opened {
  static constexpr std::string_view name = "a room's settings opening";
};
// How the dialog is drawn for a room:
//   room_presented(c, f, facts, palette) -> std::optional<room_manage_style>,
//   none where the room is not its own (with its UI).
struct presentation {
  static constexpr std::string_view name = "a room's settings' look";
};
// Sections of the client's General, after its own (a forum's switch):
//   extend(c, f, palette, facts) -> its section, shown or not as the facts
//   say -- made where the window makes the dialog
struct general_sections {
  static constexpr std::string_view name = "a room's settings' General sections";
};
}  // namespace contract

consteval bool provides(::mux::contract::window_dialogs, type_tag<feature>) { return true; }
consteval bool provides(::mux::contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::room_settings
