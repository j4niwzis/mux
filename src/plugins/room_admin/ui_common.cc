// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_admin.ui:common -- How a room's settings are drawn for a
// room of a protocol's whose realization says (contract::presentation):
// the first that does.
export module mux.plugin.room_admin.ui:common;

import std;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.plugin.room_settings.types;
import mux.plugin.room_settings.ui;
import mux.plugin.room_admin.types;

export namespace mux::plugins::room_admin {

template <class... Rs>
std::optional<room_settings::room_manage_style> presented_by(realization_list<Rs...>, const room_settings::room_settings_facts& facts,
                                                             const auto& colours) {
  std::optional<room_settings::room_manage_style> found;
  ((found = found ? found : admin_presented(Rs{}, facts, colours)), ...);
  return found;
}
template <class Colours>
std::optional<room_settings::room_manage_style> room_presented(room_settings::contract::presentation, type_tag<feature>,
                                                               const room_settings::room_settings_facts& facts, const Colours& colours) {
  return presented_by(realizations{}, facts, colours);
}

}  // namespace mux::plugins::room_admin
