// SPDX-License-Identifier: AGPL-3.0-only
// A message's bubble, made in a unit of its own: held outside a release build,
// so that no unit that shows messages makes its tables.
module mux.ui.messages;

import std;
import skiff.scene;
import skiff.compose;
import mux.core;
import mux.ui.base;

namespace mux::ui {
skiff::compose::Held message_body_held(const bubble_needs& n, const conversation& in, const bubble_facts& f, const model* now) {
  // Outside a release build a piece's rules are the table's: what holds
  // pieces (a link card's face) is built here under it.
  return skiff::compose::Held(skiff::compose::erased(skiff::compose::built(message_body(n, in, f, now), skiff::compose::table_rules{})));
}
}  // namespace mux::ui
