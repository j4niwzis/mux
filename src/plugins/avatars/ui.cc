// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.avatars.ui -- The faces where the core puts them (contract::faces):
// a person's or a chat's picture, placed and sized by the core.
export module mux.plugin.avatars.ui;

import std;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.plugin.avatars.types;

export namespace mux::plugins::avatars {

using namespace ::mux::ui;

template <class Kind>
auto extend(contract::faces, type_tag<feature>, const palette&, Kind kind, const std::string& id, std::string_view name, float size) {
  return avatar_mark(kind, id, name, size);
}

}  // namespace mux::plugins::avatars
