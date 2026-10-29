// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.looks.types -- What the looks plugin names, and what it says it
// provides: its pickers on the pages a level's looks are chosen on, and a
// part of the program. Light: mux.ui sees it.
export module mux.plugin.looks.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::looks {

struct feature {
  static constexpr std::string_view name = "looks";
};

consteval bool provides(contract::level_sections, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::looks
