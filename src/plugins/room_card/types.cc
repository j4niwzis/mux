// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_card.types -- What the room card plugin names, and what it
// says it provides: the card's dialog, and a part of the program. Light:
// mux.ui sees it.
export module mux.plugin.room_card.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::room_card {

struct feature {
  static constexpr std::string_view name = "room card";
};

// A room's card open: what was asked for, and what is known of it so far.
struct shown {
  std::string asked;
  room_preview known;
  friend bool operator==(const shown&, const shown&) = default;
};

// Asked of the program, from the card: joined, knocked on, its invite
// declined.
struct join {};
struct knock {};
struct decline {};

consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::room_card
