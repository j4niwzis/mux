// SPDX-License-Identifier: AGPL-3.0-only
// The erased walks of some of the window's subtrees, made here alone (see
// requests.cc): built only where walks are erased, outside a release build.
import std;
import skiff.scene;
import mux.ui;
import mux.app.requests;

template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::conversations_screen<mux::app::actions>>() noexcept;
template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::drawer_panel<mux::app::actions>>() noexcept;
template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::accounts_panel<mux::app::actions>>() noexcept;
