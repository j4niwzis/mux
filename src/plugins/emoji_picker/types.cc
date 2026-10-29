// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.emoji_picker.types -- What the emoji picker names, and what it
// says it provides: the media panel -- its emoji, and the pages plugins give
// it (contract::media_pages) -- a layer over the window, and a part of the
// program. Light: mux.ui sees it. (What is shown of it, emoji_facts, is
// its UI's: it holds every media page's.)
export module mux.plugin.emoji_picker.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::emoji_picker {

struct feature {
  static constexpr std::string_view name = "emoji picker";
};

consteval bool provides(contract::window_layers, type_tag<feature>) { return true; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::emoji_picker
