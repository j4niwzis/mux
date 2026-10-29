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

// What the bar says, as the program has it: which found is shown, of how
// many -- none asked yet, nothing said -- and whether it is open, in place
// of the head.
struct shown {
  std::optional<std::size_t> at;
  std::size_t of = 0;
  bool asked = false;
  bool open = false;
  friend bool operator==(const shown&, const shown&) = default;
};

consteval bool provides(contract::chat_header, type_tag<feature>) { return true; }
consteval bool provides(contract::chat_column, type_tag<feature>) { return true; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::search
