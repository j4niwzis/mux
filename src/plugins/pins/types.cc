// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.pins.types -- What the pins plugin names, and what it says it
// provides: the pinned bar in the chat's column, what it shows among what
// the window shows, Pin and Unpin in a message's menu, and a part of the
// program. Light: mux.ui sees it.
export module mux.plugin.pins.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::pins {

struct feature {
  static constexpr std::string_view name = "pins";
};

// The pin the bar shows: which message, "Pinned message" -- "#2 of 3"
// where there are more -- and a line of it.
struct pin_view {
  std::string id;
  std::string title;
  std::string line;
  friend bool operator==(const pin_view&, const pin_view&) = default;
};
// What the bar shows, as the program has it: the pin above the view; none,
// and no bar.
struct shown {
  std::optional<pin_view> pin;
  friend bool operator==(const shown&, const shown&) = default;
};

// Asked of the program: the bar pressed (to the pin it shows, and the bar
// on to the one pinned before it); swiped across (the pin before -- older
// -- or after it, in the bar only); a message pinned or unpinned, from its
// menu.
struct pressed {};
struct step {
  bool older = true;
};
struct pin {
  std::string message;
  bool on = true;
};

consteval bool provides(contract::chat_column, type_tag<feature>) { return true; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::message_menu, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::pins
