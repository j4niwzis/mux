// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.edit_history.types -- What the edit history plugin names, and
// what it says it provides: the Edit History row of a message's menu, the
// dialog that lists the message's versions, and a part of the program.
// Light: mux.ui sees it.
export module mux.plugin.edit_history.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::edit_history {

struct feature {
  static constexpr std::string_view name = "edit history";
};

// A message's earlier versions open: in which chat, the message as it is
// now, and the chats it is read from.
struct shown {
  conversation_id in;
  message said;
  const model* known = nullptr;
};
// Asked of the program: a message's versions shown, by its id.
struct show {
  std::string message;
};

consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::edit_history
