// SPDX-License-Identifier: AGPL-3.0-only
// The XMPP room's roles page, made in a unit of its own: held erased outside a
// release build, so the settings dialog's unit does not make its tables.
module mux.ui.proto.xmpp;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import mux.ui.base;

namespace mux::proto::xmpp::manage {
erased_page manage_page_erased(roles tab, const ui::palette& colours, const ui::room_settings_facts& facts) {
  return erased_page(page_of(tab, colours, facts));
}
}  // namespace mux::proto::xmpp::manage
