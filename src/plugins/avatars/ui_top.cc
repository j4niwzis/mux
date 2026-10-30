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
// A search result's: the picture of who said it, then the text.
struct beside_sender {
  auto operator()(auto text, const palette&, const search_result& one) const {
    namespace c = skiff::compose;
    return c::row(c::hbox(10.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                  c::styled({.alignSelf = scene::align::kMiddle}, avatar_mark(avatar_of::person{}, one.sender, one.name, 40.0f)),
                  std::move(text));
  }
};
// A picture of `size`, then the text, `gap` apart: a row that grows across,
// as the text did.
template <class Kind>
auto picture_beside(auto text, Kind kind, const std::string& id, std::string_view name, float size, float gap) {
  namespace c = skiff::compose;
  return c::row(c::hbox(gap, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                c::styled({.alignSelf = scene::align::kMiddle}, avatar_mark(kind, id, name, size)), std::move(text));
}
// An account in the drawer; who read a message; a room in a directory.
struct beside_account {
  auto operator()(auto text, const palette&, const drawer_account& one) const {
    return picture_beside(std::move(text), avatar_of::person{}, one.address, one.address, 38.0f, 14.0f);
  }
};
struct beside_reader {
  auto operator()(auto text, const palette&, const seen_reader& one) const {
    return picture_beside(std::move(text), avatar_of::person{}, one.id, one.name, 30.0f, 14.0f);
  }
};
struct beside_room {
  auto operator()(auto text, const palette&, const directory_room& one) const {
    return picture_beside(std::move(text), avatar_of::chat{}, one.id, one.name.empty() ? one.alias : one.name, 40.0f, 12.0f);
  }
};
constexpr auto shape_rules(contract::shapes, type_tag<feature>) {
  using namespace shaping;
  return on<chat_row_text_fn>(around(beside_avatar{})) | on<found_row_text_fn>(around(beside_sender{})) |
         on<drawer_account_text_fn>(around(beside_account{})) | on<reader_text_fn>(around(beside_reader{})) |
         on<directory_room_text_fn>(around(beside_room{}));
}

}  // namespace mux::plugins::avatars
