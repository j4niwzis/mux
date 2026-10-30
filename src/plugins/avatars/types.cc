// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.avatars.types -- What the avatars plugin names, and what it
// says it provides: rules shaping the window's pieces (its UI_TOP layer's).
export module mux.plugin.avatars.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::avatars {

struct feature {
  static constexpr std::string_view name = "avatars";
};
consteval bool provides(contract::shapes, type_tag<feature>) { return true; }

}  // namespace mux::plugins::avatars
