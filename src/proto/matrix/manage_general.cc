// SPDX-License-Identifier: AGPL-3.0-only
// The general room page, instantiated independently of the settings dialog.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import mux.ui.base;

namespace mux::proto::matrix::manage {
using general_page_t = decltype(manage_detail::general_page(std::declval<manage_detail::manage_box*>(), std::declval<const ui::room_settings_facts&>()));
erased_page manage_page_erased(room, manage_detail::manage_box* box, const ui::room_settings_facts& facts) {
  return erased_page(manage_detail::general_page(box, facts));
}
bool place_general(manage_detail::manage_box& box, erased_page& page) {
  if (auto* shown = page.shown().get<general_page_t>()) {
    manage_detail::manage_page_placed(box, *shown);
    return true;
  }
  return false;
}
bool preserve_general(erased_page& page) {
  if (auto* shown = page.shown().get<general_page_t>()) {
    manage_detail::preserve_manage_page(*shown);
    return true;
  }
  return false;
}
}
