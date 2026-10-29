// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.types -- What the typing plugin names, and what it says
// it provides: the chat's status in its head while someone types, and a
// part of the program. Light: mux.ui sees it.
export module mux.plugin.typing.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::typing {

struct feature {
  static constexpr std::string_view name = "typing";
};

consteval bool provides(contract::chat_status, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::typing
