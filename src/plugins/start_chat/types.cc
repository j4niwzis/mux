// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.start_chat.types -- What the start chat plugin names, and what
// it says it provides: its row in the drawer, the dialog someone is chosen
// in -- to talk to, or to invite to a room -- and a part of the program.
// Light: mux.ui sees it.
export module mux.plugin.start_chat.types;

import std;
import skiff.model;
import mux.core;
import mux.composition;

export namespace mux::plugins::start_chat {

struct feature {
  static constexpr std::string_view name = "start chat";
};

// Start chat open: the people known, one's own link, those found for what
// is typed, by which account; the rows shown; and, inviting to a room
// instead, which and its name.
struct shown {
  std::vector<found_person> people;
  std::string own_link;
  std::vector<found_person> found;
  std::string query;
  std::optional<account_id> by;
  skiff::model::Keyed<std::string, found_person> rows;
  std::optional<conversation_id> inviting;
  std::string room_name;
};

// Asked of the program: someone chosen -- the chat with them started, or
// them invited to the room the dialog is for.
struct pick {
  std::string user;
};

consteval bool provides(contract::drawer_rows, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::start_chat
