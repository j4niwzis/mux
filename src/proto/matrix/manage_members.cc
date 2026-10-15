// SPDX-License-Identifier: AGPL-3.0-only
// The members room page, instantiated independently of the settings dialog.
module mux.ui.proto.matrix;

import std;
import skiff.scene;
import skiff.compose;
import mux.ui.base;

namespace mux::proto::matrix::manage {
using members_page_t = decltype(manage_detail::members_page(std::declval<manage_detail::manage_box*>(), std::declval<const ui::room_settings_facts&>()));
erased_page manage_page_erased(members, manage_detail::manage_box* box, const ui::room_settings_facts& facts) {
  return erased_page(manage_detail::members_page(box, facts));
}
bool place_members(manage_detail::manage_box& box, erased_page& page) {
  if (auto* shown = page.shown().get<members_page_t>()) {
    manage_detail::manage_page_placed(box, *shown);
    return true;
  }
  return false;
}
bool preserve_members(erased_page& page) {
  if (auto* shown = page.shown().get<members_page_t>()) {
    manage_detail::preserve_manage_page(*shown);
    return true;
  }
  return false;
}
}
