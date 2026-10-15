// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// The window's layers: the frame, every dialog's shell, the popups -- so that the scene's own unit walks the window alone.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::window_body_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::window_body_t>();
}
