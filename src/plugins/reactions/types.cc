// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.reactions.types -- What the reactions plugin says it provides:
// a message's trailer, its strip of reactions. Light: mux.ui sees it.
export module mux.plugin.reactions.types;

import std;
import mux.composition;

export namespace mux::plugins::reactions {

struct feature {
  static constexpr std::string_view name = "reactions";
};

consteval bool provides(contract::message_trailers, type_tag<feature>) { return true; }

}  // namespace mux::plugins::reactions
