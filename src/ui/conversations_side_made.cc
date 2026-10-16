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
  return skiff::compose::Held(skiff::compose::erased(side_column(needs)));
}
}  // namespace mux::ui
