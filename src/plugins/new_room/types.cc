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
// Create a room open, by a protocol's account, as its own type: from the
// drawer, or in a space -- all it shows known from the type, the space's
// choice only in a space's.
template <class Protocol>
struct room_form_of {
  using protocol = Protocol;
  std::string own_server;
  account_id by;
  friend bool operator==(const room_form_of&, const room_form_of&) = default;
};
template <class Protocol>
struct space_room_form_of {
  using protocol = Protocol;
  std::string own_server;
  account_id by;
  place in;
  friend bool operator==(const space_room_form_of&, const space_room_form_of&) = default;
};

consteval bool provides(contract::drawer_rows, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::new_room
