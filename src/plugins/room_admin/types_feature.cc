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
template <class... Lists>
struct pages_joined {
  using type = composition::page_list<>;
};
template <class... A>
struct pages_joined<composition::page_list<A...>> {
  using type = composition::page_list<A...>;
};
template <class... A, class... B, class... Rest>
struct pages_joined<composition::page_list<A...>, composition::page_list<B...>, Rest...>
    : pages_joined<composition::page_list<A..., B...>, Rest...> {};
template <class... Rs>
constexpr typename pages_joined<typename Rs::tabs...>::type tabs_of(realization_list<Rs...>) { return {}; }
template <class... Rs>
constexpr typename pages_joined<typename Rs::kept...>::type kept_of(realization_list<Rs...>) { return {}; }
template <class Facts, class... Rs>
void opened_by(realization_list<Rs...>, Facts& facts, const conversation& chat) {
  (Rs::opened(facts, chat), ...);
}

consteval bool provides(room_settings::contract::tabs, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::kept, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::opened, type_tag<feature>) { return true; }
consteval bool provides(room_settings::contract::presentation, type_tag<feature>) { return true; }
consteval bool provides(::mux::contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::room_admin
