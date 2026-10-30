// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.avatars.ui_top -- A chat's picture beside its row's text in the
// list: a rule on the text piece (mux.ui.lists' chat_row_text), around it --
// the two side by side, as the row had them when it drew its own.
export module mux.plugin.avatars.ui_top;

import std;
import skiff.scene;
import skiff.compose;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.ui.lists;
import mux.plugin.avatars.types;

export namespace mux::plugins::avatars {

using namespace ::mux::ui;

// The picture, then the text: a row that grows across, as the text did.
struct beside_avatar {
  auto operator()(auto text, const ui_needs&, const conversation& one, const conversation_view& view, const chat_preview&) const {
    namespace c = skiff::compose;
    return c::row(c::hbox(12.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                  avatar_mark(avatar_of::chat{}, one.id.id, view.name, 46.0f), std::move(text));
  }
};
constexpr auto shape_rules(contract::shapes, type_tag<feature>) {
  return shaping::on<chat_row_text_fn>(shaping::around(beside_avatar{}));
}

}  // namespace mux::plugins::avatars
