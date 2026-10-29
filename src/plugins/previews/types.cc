// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.previews.types -- What the link previews plugin names, and what
// it says it provides: a card in a message's bubble. Light: mux.ui sees it.
export module mux.plugin.previews.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::previews {

struct feature {
  static constexpr std::string_view name = "previews";
};

consteval bool provides(contract::message_cards, type_tag<feature>) { return true; }

}  // namespace mux::plugins::previews
