// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.types:feature -- What the room admin plugin is, and
// what it says it provides: a room's admin tabs in its settings (the
// room_settings plugin's contracts), one realization for each protocol
// turned on (realizations, in the module CMake writes), and a part of the
// program.
export module mux.plugin.room_admin.types:feature;

import std;
import mux.core;
import mux.composition;
import mux.plugin.room_settings.types;

export namespace mux::plugins::room_admin {

struct feature {
  static constexpr std::string_view name = "room admin";
};

// The protocols' realizations: each its tabs, what it keeps, and what it
// fills as the settings open (opened(facts, chat)).
template <class... Realizations>
struct realization_list {};

consteval bool provides(room_settings::contract::tabs, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::kept, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::opened, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::presentation, type_tag<feature>) { return true; }
consteval bool provides(::mux::contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::room_admin
