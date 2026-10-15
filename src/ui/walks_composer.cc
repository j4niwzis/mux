// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui.messages, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// The chat's composer: its field, its bars, its buttons.
module mux.ui.messages;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::composer_bar_t<mux::ui::in_chat>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::composer_bar_t<mux::ui::in_chat>>();
}
