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

// A row of the list: what it shows at its start, its text beside it;
// pressed to pick.
template <class Pick, class Face>
auto suggestion_row(const palette& colours, Pick pick, Face face, std::string first, std::string second) {
  namespace c = skiff::compose;
  auto label = first;
  return SKIFF_NAMED(suggestion_row, c::onPress(std::move(pick), c::row(
      c::hbox(10.0f, {.fillX = true, .height = 44.0f, .padding = {0.0f, 14.0f, 0.0f, 14.0f},
                     .hoverBackground = colours.chosen, .selectedBackground = colours.chosen}),
      std::move(face), two_lines(colours, std::move(first), std::move(second), 14.0f, 1.0f)), std::move(label)));
}
using suggestion_face = spl::variant<nodes::Text, nodes::Image<from_emotes>, avatar_mark_t<avatar_of::person>>;
[[nodiscard]] inline suggestion_face face_of(const palette& colours, const suggestion& one) {
  if (one.person)
    return suggestion_face(std::in_place_index<2>, avatar_mark(avatar_of::person{}, one.id, one.title, 28.0f));
  if (one.picture.empty())
    return suggestion_face(std::in_place_index<0>, nodes::Text(one.glyph, 22.0f, colours.text));
  suggestion_face face(std::in_place_index<1>, from_emotes{one.picture});
  spl::get<1>(face).apply({.width = 28.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle});
  return face;
}
// Its list, over the composer (mux.ui:conversations_screen).
inline auto extend(contract::composer_top, type_tag<feature>, const palette& colours) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(suggestions_for, c::shown_for<shown>([](const shown& now) { return !now.items.empty(); },
      c::projected<shown>([](const shown& now) { return now.items; },
          c::listed<suggestion>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .background = colours.sidebar}),
              [&colours](const suggestion& one) {
                auto row = suggestion_row(colours, sends<request::suggestion_pick>{{one.at}}, face_of(colours, one), one.title, one.sub);
                row.apply({.selected = one.lit});
                return row;
              }))));
}

}  // namespace mux::plugins::suggestions
