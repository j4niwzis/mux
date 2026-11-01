// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.search.types -- What the search plugin names, and what it says
// it provides: its button in the chat's head, its bar in the chat's column,
// what the bar says among what the window shows, and a part of the program.
// Light: mux.ui sees it.
export module mux.plugin.search.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::search {

struct feature {
  static constexpr std::string_view name = "search";
};

// A message found, as its list down the side shows it: its place among
// them, who said it, when, and its words around what was found.
struct found_line {
  std::size_t index = 0;
  std::string sender, name;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  std::string snippet;
  friend bool operator==(const found_line&, const found_line&) = default;
};
// What it shows, as the program has it: which found is shown, of how many
// -- none asked yet, nothing said -- and whether the bar is open, in place
// of the head; and what was found, listed down the side in the chats'
// place -- none while nothing is asked.
struct shown {
  std::optional<std::size_t> at;
  std::size_t of = 0;
  bool asked = false;
  bool open = false;
  std::optional<std::size_t> found;
  std::vector<found_line> lines;
  friend bool operator==(const shown&, const shown&) = default;
};
// Asked of the program: the bar opened (its magnifier, Ctrl+F) or closed
// (its cross, Esc); what is typed in it; a step to the older or the newer;
// one of the list picked.
struct open {};
struct close {};
struct typed {
  std::string text;
};
struct step {
  bool older = true;
};
struct pick {
  std::size_t index = 0;
};

consteval bool provides(contract::chat_header, type_tag<feature>) { return true; }
// Its bar's field takes the keys as it opens.
consteval bool provides(contract::key_focus, type_tag<feature>) { return true; }
// Ctrl+F and Esc on the chats screen (its UI layer says them).
consteval bool provides(contract::screen_keys, type_tag<feature>) { return true; }
// The head stood aside for its bar while it is open.
consteval bool provides(contract::head_cover, type_tag<feature>) { return true; }
inline bool covers_head(contract::head_cover, type_tag<feature>, const shown& now) { return now.open; }
// What it found, down the side in the chats' place (its UI_TOP layer).
consteval bool provides(contract::side_panes, type_tag<feature>) { return true; }
inline bool covers_chats(contract::side_panes, type_tag<feature>, const shown& now) { return now.found.has_value(); }
consteval bool provides(contract::chat_column, type_tag<feature>) { return true; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::search
