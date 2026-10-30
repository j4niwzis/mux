// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.pins.ui -- The pinned bar, as tdesktop's under a chat's head:
// a stripe in the accent, "Pinned message" -- "#2 of 3" where there are
// more -- over a line of it. Pressed, the program goes to it, and the bar to
// the one pinned before it; swiped across, right the pin before, left the
// one after. And Pin, Unpin in a message's menu.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.pins.ui;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.pins.types;

export namespace mux::plugins::pins {
using namespace ::mux::ui;

inline constexpr float kBarHeight = 46.0f;
// Its own drag: the screen's flick (a step back, the drawer out) not made
// of it -- swiping through the pins opened the drawer too (a tester's).
struct swiped {
  std::optional<step> operator()(bool towards_left) const { return step{.older = !towards_left}; }
};
MUX_INLINE_IN_RELEASE auto pinned_bar(const palette& colours) {
  namespace c = skiff::compose;
  const auto pin = [](const shown& now) { return now.pin.value_or(pin_view{}); };
  return SKIFF_NAMED(pinned_bar, c::shown_for<shown>([](const shown& now) { return now.pin.has_value(); },
      c::flicked_holding(60.0f, swiped{}, c::onClick(pressed{},
          c::row(c::hbox(10.0f, {.fillX = true, .height = kBarHeight, .padding = {5.5f, 16.0f, 6.0f, 18.0f},
                                 .background = colours.sidebar, .hoverBackground = colours.chosen}),
                 c::styled({.fillY = true, .width = 2.0f, .cornerRadius = 1.0f}, nodes::Box<>(colours.accent)),
                 c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                           c::text_for<shown>([pin](const shown& now) { return pin(now).title; },
                               c::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.accent)))),
                           c::text_for<shown>([pin](const shown& now) { return pin(now).line; },
                               c::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.dim))))),
                 c::styled({.place = scene::anchor::kBottomLeft, .fillX = true, .height = 1.0f}, nodes::Box<>(colours.band))),
          "Pinned message"))));
}

// The bar in the chat's column, under its head (mux.ui:conversations_screen).
MUX_INLINE_IN_RELEASE auto extend(contract::chat_column, type_tag<feature>, const ui_needs& needs) { return pinned_bar(*needs.colours); }

// Pin or Unpin, in a message's menu, where its chat keeps pins.
MUX_INLINE_IN_RELEASE auto extend(contract::message_menu, type_tag<feature>, const palette& colours, const emoji_kept&, const menu_facts& facts) {
  return skiff::compose::visible(facts.pinnable,
      row_item(colours, facts.pinned ? "Unpin" : "Pin", sends<pin>{pin{facts.id, !facts.pinned}}, icon::check{}, std::nullopt, menu_row_look));
}

}  // namespace mux::plugins::pins
