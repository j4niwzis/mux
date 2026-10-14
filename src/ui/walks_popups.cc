// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// The context menu and the picture viewer.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::context_menu>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::context_menu>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::picture_viewer>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::picture_viewer>();
}
