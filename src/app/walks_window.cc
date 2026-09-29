// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of the window's layers -- its frame, its dialogs' shells,
// its popups -- made here alone (see requests.cc): built only where walks
// are erased, outside a release build.
// Part of mux.app.requests, where this table is declared.
module mux.app.requests;
import std;
import skiff.scene;
import mux.ui;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::window<mux::app::actions>::layers>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::window<mux::app::actions>::layers>();
}
