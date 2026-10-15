// SPDX-License-Identifier: AGPL-3.0-only
// Debug walk tables for the side column's menus and space bar rows, away
// and virtualized rows. Release still walks the composed tree statically.
module mux.ui;
import std;
import skiff.scene;

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_space_menu_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_space_menu_t>();
}
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_chat_menu_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_chat_menu_t>();
}

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_space_rows_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_space_rows_t>();
}

template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::side_folder_rows_t>() noexcept {
  return skiff::scene::AnyNode::opsOf<mux::ui::side_folder_rows_t>();
}
