// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// The chat's info and its threads' panel.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::info_panel_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::info_panel_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::threads_panel_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::threads_panel_t>();
}
