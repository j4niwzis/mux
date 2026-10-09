// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone (see
// requests.cc): built only where walks are erased, outside a release build.
// Part of mux.app.requests, where these tables are declared: a definition
// outside that module would be another entity (module attachment).
module mux.app.requests;
import std;
import skiff.scene;
import mux.ui;
import mux.ui.proto;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::explore_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::explore_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::start_chat_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::start_chat_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::wallpaper_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::wallpaper_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::packs_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::packs_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::create_room_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::create_room_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::person_card<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::person_card<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_card<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::room_card<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::reactions_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::reactions_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::marks_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::marks_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::forward_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::forward_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::send_box<mux::app::actions>>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::send_box<mux::app::actions>>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::notice_box_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::notice_box_t>();
}
