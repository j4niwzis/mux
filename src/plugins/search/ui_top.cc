// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.search.ui_top -- What a chat's search found, listed down the
// side in the chats' place (contract::side_panes), as tdesktop lists it: how
// many, then who, when and their words around what was asked -- each line a
// piece the plugins shape (mux.ui.lists' message_line_text: the avatars
// plugin's picture beside it); pressed, gone to.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.search.ui_top;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.ui.lists;
import mux.plugin.search.types;

export namespace mux::plugins::search {
using namespace ::mux::ui;

MUX_INLINE_IN_RELEASE auto found_row(const palette& colours, const found_line& one) {
  namespace c = skiff::compose;
  return c::with_cursor(scene::cursor::arrow{}, c::onClick(pick{one.index},
      c::row(c::hbox(10.0f, {.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 10.0f}, .hoverBackground = colours.chosen}),
             message_line_text(colours, message_line{one.index, one.sender, one.name, one.at, one.snippet})),
      std::format("{}: {}", one.name, one.snippet)));
}
struct found_row_make {
  const palette* colours;
  auto operator()(const std::pair<std::size_t, found_line>& item) const { return found_row(*colours, item.second); }
};
// How many were found, over them; each line kept while it says the same.
MUX_INLINE_IN_RELEASE auto extend(contract::side_panes, type_tag<feature>, const palette& colours) {
  namespace c = skiff::compose;
  auto contents = c::projected<shown>([](const shown& now) {
    return now.lines | std::views::transform([](const found_line& one) { return std::pair{one.index, one}; }) | std::ranges::to<std::vector>();
  }, c::styled({.fillX = true, .autoSize = scene::axes::kY},
               c::memo_rows<std::size_t, found_line, std::pair<std::size_t, found_line>>(found_row_make{&colours}, nodes::Stack{})));
  return SKIFF_NAMED(found_pane, c::shown_for<shown>([](const shown& now) { return now.found.has_value(); },
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
          c::styled({.margin = {4.0f, 16.0f, 6.0f, 16.0f}},
              c::text_for<shown>([](const shown& now) {
                return !now.found ? std::string{} : *now.found == 0 ? std::string("No messages found")
                       : *now.found == 1 ? std::string("1 message found") : std::format("{} messages found", *now.found);
              }, nodes::Text("", 13.0f, colours.dim, true))),
          c::projected<shown>([](const shown& now) { return now.lines; },
              c::restarted_scroll<std::vector<found_line>>(c::styled({.fillX = true, .grow = scene::axes::kY}, c::scrolled(std::move(contents))))))));
}

// Its keys on the chats screen (contract::screen_keys): Ctrl+F opens the
// bar; Esc, while it is open, closes it.
inline std::optional<std::variant<open, close>> screen_key(contract::screen_keys, type_tag<feature>, const request::screen_key& one,
                                                           const shown& now) {
  namespace keys = scene::keys;
  if (one.key == keys::kF && one.control)
    return open{};
  if (one.key == keys::kEscape && !(one.control || one.shift || one.alt) && now.open)
    return close{};
  return std::nullopt;
}

}  // namespace mux::plugins::search
