// SPDX-License-Identifier: AGPL-3.0-only
// The room packs page, made in a unit of its own: held erased outside a release
// build, so the settings dialog's unit does not make its tables.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import mux.ui.base;

namespace mux::proto::matrix::manage {
erased_page manage_page_erased(packs tab, const ui::palette& colours, const ui::room_settings_facts& facts) {
  return erased_page(page_of(tab, colours, facts));
}
}  // namespace mux::proto::matrix::manage
