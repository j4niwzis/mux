// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.avatars.ui -- A sender's face beside their messages in a group
// (contract::message_faces): their picture, where the core places it.
export module mux.plugin.avatars.ui;

import std;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.plugin.avatars.types;

export namespace mux::plugins::avatars {

using namespace ::mux::ui;

inline auto extend(contract::message_faces, type_tag<feature>, const palette&, const std::string& sender, const std::string& name, float size) {
  return avatar_mark(avatar_of::person{}, sender, name, size);
}

}  // namespace mux::plugins::avatars
