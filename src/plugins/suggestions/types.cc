// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.suggestions.types -- What the suggestions plugin names, and
// what it says it provides: its list over the composer, what it lists among
// what the window shows, and a part of the program. Light: mux.ui sees it.
export module mux.plugin.suggestions.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::suggestions {

struct feature {
  static constexpr std::string_view name = "suggestions";
};

// One of the list over the composer, as the program has it: a member (their
// avatar, their name, their id) or an emoji (its picture or character, its
// name, its code), where it is in the list, and whether it is lit.
struct suggestion {
  std::size_t at = 0;
  bool person = false;
  std::string id;       // whose avatar, a member's
  std::string glyph;    // the emoji's character
  std::string picture;  // a custom emoji's picture
  std::string title, sub;
  bool lit = false;
  friend bool operator==(const suggestion&, const suggestion&) = default;
};
struct shown {
  std::vector<suggestion> items;
  friend bool operator==(const shown&, const shown&) = default;
};

consteval bool provides(contract::composer_top, type_tag<feature>) { return true; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::suggestions
