// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone (see
// requests.cc): built only where walks are erased, outside a release build.
// Part of mux.app.requests, where these tables are declared: a definition
// outside that module would be another entity (module attachment).
// The chat column: the timeline, its bars, the search -- all but the
// bubbles and the composer, which are walked in units of their own.
module mux.app.requests;
import std;
import skiff.scene;
import mux.ui;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>::chat_column>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::conversations_screen<mux::app::actions>::chat_column>();
}
