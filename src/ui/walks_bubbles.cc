// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui.messages, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// A message bubble and all its parts -- the most of any one type.
module mux.ui.messages;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::legacy::message_bubble>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::legacy::message_bubble>();
}
