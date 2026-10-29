// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.replies.ui -- Reply and Quote & Reply, in a message's menu
// (contract::message_menu): Reply where the message can be answered; Quote &
// Reply where some of its text, someone else's, is selected.
export module mux.plugin.replies.ui;

import std;
import mux.composition;
import mux.core;
import skiff.scene;
import skiff.compose;
import mux.ui.base;
import mux.plugin.replies.types;

export namespace mux::plugins::replies {

inline auto extend(contract::message_menu, type_tag<feature>, const ui::palette& colours, const ui::emoji_kept&,
                   const ui::menu_facts& facts) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = skiff::scene::axes::kY}),
      c::visible(facts.answerable, ui::row_item(colours, "Reply", ui::sends<reply>{}, ui::icon::back{}, std::nullopt, ui::menu_row_look)),
      c::visible(facts.answerable && facts.selection && !facts.own && !facts.copied.empty(),
                 ui::row_item(colours, "Quote & Reply", ui::sends<quote_reply>{}, ui::icon::back{}, std::nullopt, ui::menu_row_look)));
}

}  // namespace mux::plugins::replies
