// SPDX-License-Identifier: AGPL-3.0-only
// The three independently bound scroll views of the side column.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_chat_list_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_chat_list_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_found_list_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_found_list_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_server_list_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_server_list_t>();
}

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_header_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_header_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_body_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_body_t>();
}
