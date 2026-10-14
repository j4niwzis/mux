// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// The chat list column: its rows, the space bars, their menus, the search.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen::side_column>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::conversations_screen::side_column>();
}
