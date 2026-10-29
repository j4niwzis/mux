// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.spaces.types -- What the spaces plugin names, and what it says
// it provides: what a space is to the chat list. Light: mux.ui sees it.
export module mux.plugin.spaces.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::spaces {

struct feature {
  static constexpr std::string_view name = "spaces";
};

consteval bool provides(contract::chat_folders, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// Its box for leaving a space (its facts and box with its pieces above mux.ui.base).
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
consteval bool provides(contract::side_bars, type_tag<feature>) { return true; }

}  // namespace mux::plugins::spaces
