// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types.
// An account's pages: what the accounts panel shows of one, each its own.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::account_editor>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::account_editor>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::account_privacy>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::account_privacy>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::account_chats>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::account_chats>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::account_notifications>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::account_notifications>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::account_proxy>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::account_proxy>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::add_account_pane>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::add_account_pane>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::look_choices>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::look_choices>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::bubbles_picker>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::bubbles_picker>();
}
