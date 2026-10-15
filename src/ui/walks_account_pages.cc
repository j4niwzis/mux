// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types.
// An account's pages: what the accounts panel shows of one, each its own.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::detail_shown_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::detail_shown_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::add_account_view_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::add_account_view_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::look_choices_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::look_choices_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::bubbles_picker_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::bubbles_picker_t>();
}
