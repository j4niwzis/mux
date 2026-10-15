// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone:
// built only where walks are erased, outside a release build. Part of
// mux.ui, where these tables are declared beside their types: a
// definition outside that module would be another entity.
// The dialogs' contents: each box the window's layers show.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::explore_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::explore_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::start_chat_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::start_chat_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::wallpaper_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::wallpaper_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::packs_box_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::packs_box_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::create_room_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::create_room_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::person_card>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::person_card>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_card>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::room_card>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::edit_history_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::edit_history_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::marks_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::marks_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::forward_box>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::forward_box>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::send_box_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::send_box_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::notice_box_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::notice_box_t>();
}
