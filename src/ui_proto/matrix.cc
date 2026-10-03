// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.proto.matrix -- What Matrix shows of its own in the client's UI:
// its overloads on its tag, found by ADL where the UI is made (the program,
// which imports mux.ui.proto). Manage room's tabs of Matrix's: a room's
// name, topic and addresses; its join rule, history and encryption; its
// power levels; its version.
export module mux.ui.proto.matrix;

import std;
import mux.core;
import mux.ui;

export namespace mux::matrix {

constexpr ui::manage_tab_list<ui::settings_tab::general, ui::settings_tab::security, ui::settings_tab::roles,
                              ui::settings_tab::advanced>
manage_tabs(tag) {
  return {};
}

}  // namespace mux::matrix
