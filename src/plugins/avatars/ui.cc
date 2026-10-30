// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.avatars.ui -- A chat's picture beside its row's text in the
// list: the row's text piece (views::chat_row) shaped into the two, side by
// side, as the row had them when it drew its own.
module;
#include "skiff/named.h"
export module mux.plugin.avatars.ui;

import std;
import skiff.scene;
import skiff.compose;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.plugin.avatars.types;

export namespace mux::plugins::avatars {

using namespace ::mux::ui;

template <class Node>
auto extend(contract::shape<views::chat_row>, type_tag<feature>, Node text, const views::chat_row_facts& facts) {
  namespace c = skiff::compose;
  return c::row(c::hbox(12.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                avatar_mark(avatar_of::chat{}, facts.id, facts.name, 46.0f), std::move(text));
}

}  // namespace mux::plugins::avatars
