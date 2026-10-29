// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.devtools.types -- What the developer tools plugin names, and
// what it says it provides: View Source in a message's menu, a room's
// settings' tab, the dialog the tools are shown in, and a part of the
// program. Light: mux.ui sees it.
export module mux.plugin.devtools.types;

import std;
import splice;
import knot;
import mux.core;
import mux.composition;
import mux.proto;
import mux.plugin.room_settings.types;
export import mux.plugin.devtools.changes;

export namespace mux::plugins::devtools {

struct feature {
  static constexpr std::string_view name = "devtools";
};

// What the dialog has open: some JSON to read (a source, an answer); the
// room's state, by type and key; its account data; a state event or
// account data being written (`fresh`: a new one, its type and key typed
// too); an event of any type being composed.
namespace screen {
struct reading {
  std::string title, text;
  friend bool operator==(const reading&, const reading&) = default;
};
struct state {
  std::vector<state_entry> entries;
  friend bool operator==(const state&, const state&) = default;
};
struct data {
  std::vector<data_entry> entries;
  friend bool operator==(const data&, const data&) = default;
};
struct state_editor {
  std::string type, key, content;
  bool fresh = false;
  friend bool operator==(const state_editor&, const state_editor&) = default;
};
struct data_editor {
  std::string type, content;
  bool fresh = false;
  friend bool operator==(const data_editor&, const data_editor&) = default;
};
struct composer {
  friend bool operator==(composer, composer) = default;
};
}  // namespace screen
using screen_t = spl::variant<screen::reading, screen::state, screen::data, screen::state_editor, screen::data_editor, screen::composer>;

// The tools open: for a room, on one screen -- each opening a number of its
// own, so that a screen made again is made anew.
struct shown {
  conversation_id in;
  screen_t screen;
  std::uint64_t opened = 0;
  friend bool operator==(const shown&, const shown&) = default;
};

// A room's settings' tab: the tools for it.
struct room_tab {};

// Asked of the program: a message's source (its menu); the room's state,
// its account data, an event composed (the room's settings' tab); a state
// event or account data opened to write -- a new one where no type is
// said; written; an event sent.
struct view_source {
  std::string message;
};
struct open_state {};
struct open_data {};
struct open_composer {};
struct edit_state {
  std::string type, key;
};
struct edit_data {
  std::string type;
};
struct save_state {
  std::string type, key, content;
};
struct save_data {
  std::string type, content;
};
struct send_event {
  std::string type;
  std::optional<std::string> key;
  std::string content;
};

consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
// Its tab of a room's settings (the room_settings plugin's).
consteval bool provides(room_settings::contract::tabs, type_tag<feature>) { return true; }
// Put on the settings of every protocol's rooms whose protocol has developer tools,
// known from its type while compiling.
template <class Protocol>
  requires(proto::offered_by<state_of<Protocol>, proto::feature::developer_tools>())
constexpr composition::page_list<room_tab> room_tabs(room_settings::contract::tabs, type_tag<feature>,
                                                     type_tag<room_settings::room_kind::room_of<Protocol>>) {
  return {};
}
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// Its own settings (contract::settings_kept): whether the tools are on --
// kept in the settings file under its name, and shown as the window's
// (contract::window_state) for its rows and tab to read.
struct kept {
  bool tools = false;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
// Asked of the program: the tools turned on or off.
struct flip_tools {};
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<kept> state_type(contract::window_state, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::devtools
