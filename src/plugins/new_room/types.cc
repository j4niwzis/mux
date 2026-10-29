// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.new_room.types -- What the new room plugin names, and what it
// says it provides: its row in the drawer, the dialog a room is made in,
// and a part of the program. Light: mux.ui sees it.
export module mux.plugin.new_room.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::new_room {

struct feature {
  static constexpr std::string_view name = "new room";
};

// A room made in a space, as its menu asks: the space, its name, and
// whether what is made is a space.
struct place {
  conversation_id space;
  std::string name;
  bool make_space = false;
  friend bool operator==(const place&, const place&) = default;
};
// Create a room open: on which server, where in a space, if in one, and by
// which account.
struct shown {
  std::string own_server;
  std::optional<place> in;
  std::optional<account_id> by;
  friend bool operator==(const shown&, const shown&) = default;
};

consteval bool provides(contract::drawer_rows, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::new_room
