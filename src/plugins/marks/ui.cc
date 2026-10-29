// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.marks.ui -- Telegram's heart and @ over the list's bottom right
// corner: reactions to the user's messages and mentions of them not seen
// yet, each with how many; pressed, the oldest gone to; pressed with the
// other button, all of them listed.
module;
#include "skiff/named.h"
export module mux.plugin.marks.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.marks.types;

export namespace mux::plugins::marks {
using namespace ::mux::ui;

template <class Count>
auto mark_button(const palette& colours, mark_kind_t which, std::string glyph, Count count) {
  namespace c = skiff::compose;
  const scene::Spec plate{.width = 42.0f, .height = 42.0f, .cornerRadius = 21.0f, .background = colours.sidebar,
                          .hoverBackground = colours.chosen, .border = scene::Border{colours.band, 1.0f}};
  return c::shown_for<shown>([count](const shown& now) { return count(now) > 0; },
      c::onRightPress(list{which},
          c::onClick(jump{which},
              c::column(c::vbox(0.0f, plate),
                        c::styled({.place = scene::anchor::kCentre}, nodes::Text(std::move(glyph), 18.0f, colours.text, true)),
                        count_badge_for<shown>(colours, count)))));
}
// Its buttons, over the list's corner (mux.ui.messages:timeline).
inline auto extend(contract::list_corner, type_tag<feature>, const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(marks_corner, c::column(c::vbox(10.0f, {.autoSize = scene::axes::kBoth}),
      mark_button(colours, mark_kind::reaction{}, "♥", [](const shown& now) { return now.reactions; }),
      mark_button(colours, mark_kind::mention{}, "@", [](const shown& now) { return now.mentions; })));
}

}  // namespace mux::plugins::marks
