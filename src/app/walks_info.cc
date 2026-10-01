// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone (see
// requests.cc): built only where walks are erased, outside a release build.
// Part of mux.app.requests, where these tables are declared: a definition
// outside that module would be another entity (module attachment).
// The chat's info and threads panels.
module mux.app.requests;
import std;
import skiff.scene;
import mux.ui;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::info_panel<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::info_panel<mux::app::actions>>();
}

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::threads_panel<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::threads_panel<mux::app::actions>>();
}
