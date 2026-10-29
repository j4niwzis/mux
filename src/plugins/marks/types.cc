// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.marks.types -- What the marks plugin names, and what it says it
// provides: its buttons over the list's corner, what they show among what
// the window shows, the dialog that lists the marks, and a part of the
// program. Light: mux.ui sees it.
export module mux.plugin.marks.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::marks {

struct feature {
  static constexpr std::string_view name = "marks";
};

// What the buttons show, as the program has it: the chat in view's
// mentions of the user, and reactions to theirs, not seen yet.
struct shown {
  std::size_t mentions = 0, reactions = 0;
  friend bool operator==(const shown&, const shown&) = default;
};
// One of a list: its message, the mark's own event, and -- a reaction's --
// who reacted, with what.
struct entry {
  message said;
  std::string event;
  std::optional<std::string> who;
  std::string key;
};
// A list of marks open: of which kind, in which chat, what it lists, and
// the chats it shows them from.
struct listed {
  mark_kind_t kind;
  conversation_id in;
  std::vector<entry> entries;
  const model* now = nullptr;
};

// Asked of the program: the oldest of a kind gone to (a press); all of them
// listed (a press with the other button); one of the list gone to.
struct jump {
  mark_kind_t kind;
};
struct list {
  mark_kind_t kind;
};
struct go_to {
  mark_kind_t kind;
  std::string event;
};

consteval bool provides(contract::list_corner, type_tag<feature>) { return true; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<listed> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::marks
