// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.notifications.types -- What the notifications plugin names,
// and what it says it provides: a part of the program. Light: mux.ui sees
// it.
export module mux.plugin.notifications.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::notifications {

struct feature {
  static constexpr std::string_view name = "notifications";
};

consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::notifications
