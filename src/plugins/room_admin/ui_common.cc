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

// A realization's look for settings not of its kind: none.
inline std::optional<room_settings::room_manage_style> admin_presented(const auto&, const auto&, const auto&) { return std::nullopt; }
template <class... Rs, class Facts>
std::optional<room_settings::room_manage_style> presented_by(realization_list<Rs...>, const Facts& facts, const auto& colours) {
  std::optional<room_settings::room_manage_style> found;
  ((found = found ? found : admin_presented(Rs{}, facts, colours)), ...);
  return found;
}
template <class Facts, class Colours>
std::optional<room_settings::room_manage_style> room_presented(room_settings::contract::presentation, type_tag<feature>,
                                                               const Facts& facts, const Colours& colours) {
  return presented_by(realizations{}, facts, colours);
}

}  // namespace mux::plugins::room_admin
