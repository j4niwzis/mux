// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.suggestions.ui -- Over the field: the members matching what
// follows an @ at the end of what is written, or the emoji matching what
// follows a ':' -- six at most, the lit one marked; pressed, picked. As the
// program says.
module;
#include "skiff/named.h"
export module mux.plugin.suggestions.ui;

import std;
import splice;
import skiff.scene;
import skiff.compose;
import skiff.nodes.image;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.suggestions.types;

export namespace mux::plugins::suggestions {
using namespace ::mux::ui;

// What a row shows at its start that is not a face: an emoji's glyph, or
// a custom one's picture; nothing (an empty glyph) for a person, whose face
// goes where faces go.
using suggestion_mark = spl::variant<nodes::Text, nodes::Image<from_emotes>>;
[[nodiscard]] inline suggestion_mark mark_of(const palette& colours, const suggestion& one) {
  if (one.person || one.picture.empty())
    return suggestion_mark(std::in_place_index<0>, nodes::Text(one.person ? std::string() : one.glyph, 22.0f, colours.text));
  suggestion_mark mark(std::in_place_index<1>, from_emotes{one.picture});
  spl::get<1>(mark).apply({.width = 28.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle});
  return mark;
}
// A row of the list: a person's face (face_spot: the avatars plugin's), or
// its mark, at its start; its text beside it; lit where it is the one
// Enter picks; pressed to pick.
template <class Pick>
auto suggestion_row(const palette& colours, Pick pick, const suggestion& one) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(suggestion_row, c::onPress(std::move(pick), c::row(
      c::hbox(10.0f, {.fillX = true, .height = 44.0f, .padding = {0.0f, 14.0f, 0.0f, 14.0f},
                     .hoverBackground = colours.chosen, .selectedBackground = colours.chosen, .selected = one.lit}),
      c::visible(one.person, face_spot(avatar_of::person{}, one.id, one.title, 28.0f)),
      c::visible(!one.person, mark_of(colours, one)),
      two_lines(colours, one.title, one.sub, 14.0f, 1.0f)), one.title));
}
// Its list, over the composer (mux.ui:conversations_screen).
inline auto extend(contract::composer_top, type_tag<feature>, const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(suggestions_for, c::shown_for<shown>([](const shown& now) { return !now.items.empty(); },
      c::projected<shown>([](const shown& now) { return now.items; },
          c::listed<suggestion>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .background = colours.sidebar}),
              [&colours](const suggestion& one) { return suggestion_row(colours, sends<request::suggestion_pick>{{one.at}}, one); }))));
}

}  // namespace mux::plugins::suggestions
