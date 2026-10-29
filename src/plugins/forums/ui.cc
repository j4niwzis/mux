// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.forums.ui -- A space's settings' switch: shown as one chat, its
// rooms as topics (room_settings' general_sections).
module;
#include "skiff/named.h"
export module mux.plugin.forums.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.model;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.plugin.forums.types;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;

export namespace mux::plugins::forums {

using namespace ::mux::ui;

template <class Colours>
auto extend(room_settings::contract::general_sections, type_tag<feature>, const Colours& colours,
            const room_settings::room_settings_facts& facts) {
  namespace c = skiff::compose;
  using toggle_t = widgets::Toggle<sends<flip_forum>>;
  return SKIFF_NAMED(forum_section, c::visible(facts.space, c::column(c::vbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      part_heading(colours, "Shown as"),
      c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .disabled = facts.holds_spaces}),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                    wrapped(nodes::Text("One chat, its rooms as topics", 14.0f, colours.text))),
          c::applied<shown>([chat = facts.chat](const shown& now) { return now.chats.contains(chat); },
                            [](toggle_t& toggle, bool on) { toggle.setOn(on); },
                            c::styled({.alignSelf = scene::align::kMiddle}, toggle_t(colours.widgets, {flip_forum{facts.chat}})))),
      explained(colours, facts.holds_spaces ? "A space that holds spaces is shown as a space."
                                            : "On: in the chat list as one chat; its rooms open inside it, as Telegram's topics."))));
}

}  // namespace mux::plugins::forums
