// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.types -- What the reactions plugin names, and what it
// says it provides: a message's trailer, its strip of reactions; the quick
// reactions over its menu, every emoji unrolled under them; the Reactions
// row of its menu, and the dialog that lists them as events; and
// a part of the program. Light: mux.ui sees it.
export module mux.plugin.reactions.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::reactions {

struct feature {
  static constexpr std::string_view name = "reactions";
};

// A reaction as the event it is: who, with what, when.
struct entry {
  std::string event;
  std::string who;
  std::string name;
  std::string key;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  bool mine = false;
  std::string to;  // the event reacted to
  std::optional<std::string> shortcode;
};
// A message's reactions open: in which chat, the reactions, the chats; and
// whether a list it was opened from is under it, to go back to.
struct shown {
  conversation_id in;
  std::vector<entry> entries;
  const model* now = nullptr;
  std::string event;
  bool back = false;
};
// Asked of the program: a message's reactions listed, by its id; and the
// list it was opened from, back.
struct show {
  std::string message;
};
struct back {};

consteval bool provides(contract::message_trailers, type_tag<feature>) { return true; }
consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
consteval bool provides(contract::message_menu_top, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::reactions
