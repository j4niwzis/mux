// SPDX-License-Identifier: AGPL-3.0-only
// The window's scene, instantiated here alone: every walk over its tree --
// routing, layout, drawing -- for every type of node in it. The other
// units see it declared extern (mux.app.requests) and call it.
import std;
import skiff.scene;
import mux.ui;
import mux.app.requests;

// The tables of the window's big subtrees -- and so the walks of all in them
// -- are made in walks_*.cc, outside a release build. Said here, in the unit
// that instantiates the scene: an extern template in an imported module
// does not keep clang from instantiating it here too.
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::conversations_screen<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::drawer_panel<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::accounts_panel<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::settings_dialog<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::room_settings<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::explore_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::new_chat_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::person_card<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::room_card<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::reactions_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::marks_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::forward_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::devtools_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::send_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::notice_box<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::emoji_popup<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::context_menu<mux::app::actions>>() noexcept;
extern template const skiff::scene::AnyNode::Ops& skiff::scene::AnyNode::opsOf<mux::ui::picture_viewer<mux::app::actions>>() noexcept;

template class skiff::scene::Scene<mux::app::window_type>;
