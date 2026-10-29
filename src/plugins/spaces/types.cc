// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.spaces.types -- What the spaces plugin names, and what it says
// it provides: what a space is to the chat list, what it keeps of the bars
// and of Home, and its switches on the pages they are chosen on. Light:
// mux.ui sees it.
export module mux.plugin.spaces.types;

import std;
import knot;
import mux.core;
import mux.config;
import mux.composition;
import mux.plugin.room_settings.types;

export namespace mux::plugins::spaces {

struct feature {
  static constexpr std::string_view name = "spaces";
};

// An account's own Home: as every account's (none), or chosen.
struct account_home {
  std::string account;
  std::optional<bool> hides_spaced, hides_direct;
  friend bool operator==(const account_home&, const account_home&) = default;
  friend consteval auto json_schema(knot::type<account_home>) { return knot::schema<account_home>(); }
};
// A space whose rooms Home leaves out, by its account and its room.
struct hidden_space {
  std::string account;
  std::string room;
  friend bool operator==(const hidden_space&, const hidden_space&) = default;
  friend consteval auto json_schema(knot::type<hidden_space>) { return knot::schema<hidden_space>(); }
};
// What it keeps (contract::settings_kept): the bars at all, and the one
// along the top; Home without what spaces hold, and without direct chats
// too; each account's own Home; where each item of the bars is put; and the
// spaces Home leaves out.
struct kept {
  bool bars = true;
  bool top_bar = true;
  bool home_hides_spaced = false;
  bool home_hides_direct = false;
  std::vector<account_home> accounts;
  std::vector<config::space_place> places;
  std::vector<hidden_space> hidden_from_home;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
// What the window shows of it for its switches (contract::window_state):
// each account's Home as its menu lists it, and the spaces Home leaves out.
struct shown {
  std::map<std::string, std::size_t> home_ways;
  std::set<conversation_id> hidden;
  friend bool operator==(const shown&, const shown&) = default;
};

consteval bool provides(contract::chat_folders, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::level_sections, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::general_sections, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// Its box for leaving a space (its facts and box with its pieces above mux.ui.base).
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
consteval bool provides(contract::side_bars, type_tag<feature>) { return true; }

}  // namespace mux::plugins::spaces
