// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone (see
// requests.cc): built only where walks are erased, outside a release build.
// Part of mux.app.requests, where these tables are declared: a definition
// outside that module would be another entity (module attachment).
module mux.app.requests;
import std;
import skiff.scene;
import mux.ui;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::context_menu<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::context_menu<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::picture_viewer<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::picture_viewer<mux::app::actions>>();
}
