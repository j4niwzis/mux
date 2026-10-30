// SPDX-License-Identifier: AGPL-3.0-only
// The chat list's side column, made in a unit of its own: held outside a
// release build, so that the screen's unit does not make its tables.
module mux.ui;

import std;
import skiff.scene;
import skiff.compose;
import mux.ui.base;

namespace mux::ui {
skiff::compose::Held side_column_held(const ui_needs& needs) {
  // Outside a release build a piece's rules are the table's: what holds
  // pieces (the chats' rows, the spaces' faces) is built here under it.
  return skiff::compose::Held(skiff::compose::erased(skiff::compose::built(side_column(needs), skiff::compose::table_rules{})));
}
}  // namespace mux::ui
