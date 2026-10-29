// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.forward.types -- What the forward plugin names, and what it
// says it provides: the Forward row of a message's menu, the selection's
// Forward, the box a chat is chosen in, and a part of the program. Light:
// mux.ui sees it.
export module mux.plugin.forward.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::forward {

struct feature {
  static constexpr std::string_view name = "forward";
};

// A chat to forward to: its id and name.
struct target {
  conversation_id id;
  std::string name;
};
// Where to forward, asked: the account's chats.
struct shown {
  std::vector<target> chats;
};
// Asked of the program: a message forwarded, by its id -- the box opened;
// and the chat chosen in it.
struct show {
  std::string message;
};
struct to {
  conversation_id chat;
};

consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
consteval bool provides(contract::selection_actions, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// What the selection bar says of it: "Forward 3"; its menu, "Forward selected".
constexpr std::string_view label(contract::selection_actions, type_tag<feature>) { return "Forward"; }

}  // namespace mux::plugins::forward
