// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.requests: What the window asks: requests, and the actions that make them.
export module mux.app.requests;

import std;
import splice;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.protocols;
import mux.app.network;

export namespace mux::app {

// The requests and what the message field is for are the window's own
// (mux.ui:requests): named here as they were.
namespace compose = mux::ui::compose;
using mux::ui::compose_t;
namespace request = mux::ui::request;

// Every request, one of them: a spl::variant, built in time linear in how
// many there are (std::variant's nested union made it quadratic).

// What the program's parts reach of the network for a message sent -- in
// the demo, put in at once, as sent.
struct actions {
  network* net = nullptr;
  // In the demo, a message sent is there at once, as sent.
  bool demo = false;
  mailbox_type* box = nullptr;
  int demo_sent = 0;

  void send(const mux::conversation_id& to, std::string text) {
    if (!demo) {
      net->send(to, std::move(text));
      return;
    }
    mux::message one;
    one.in = to;
    one.id = std::format("demo-sent-{}", demo_sent++);
    one.sender = to.account.address;
    one.at = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    one.body.plain = std::move(text);
    one.outgoing = true;
    box->push(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  }
};

using window_type = mux::ui::window<actions>;

}  // namespace mux::app

// The window's scene is instantiated once, in src/app/scene.cc: its walks
// over the whole tree -- routing, layout, drawing, for every type of node
// in it -- were most of a build, made again in every unit that touched the
// scene. In a named module, members defined in a class are not implicitly
// inline, so this keeps them all out of the other units.
extern template class skiff::scene::Scene<mux::app::window_type>;
// Outside a release build, where each child is walked through its table:
// the tables of the window's big subtrees -- and so the walks of all in
// them -- made in units of their own (walks_*.cc), in parallel, not where
// the window is walked. A release build walks statically and uses none.
// And the window's layers -- the frame, every dialog's shell, the popups --
// so that the scene's own unit walks the window alone.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::window<mux::app::actions>::layers> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::window<mux::app::actions>::layers>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>>() noexcept;
// The chats screen cut further: its heaviest subtrees each in a unit of
// their own, compiled side by side -- the one unit was six minutes.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen<mux::app::actions>::side_column> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>::side_column>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen<mux::app::actions>::chat_column> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>::chat_column>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::message_bubble<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::message_bubble<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::composer_bar<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::composer_bar<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::info_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::info_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::threads_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::threads_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::drawer_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::drawer_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::accounts_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::accounts_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::settings_dialog<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::settings_dialog<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::room_settings<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_settings<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::explore_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::explore_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::start_chat_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::start_chat_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::wallpaper_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::wallpaper_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::packs_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::packs_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::create_room_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::create_room_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::person_card<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::person_card<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::room_card<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_card<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::reactions_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::reactions_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::marks_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::marks_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::forward_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::forward_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::send_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::send_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::notice_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::notice_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::emoji_popup<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::emoji_popup<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::context_menu<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::context_menu<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::picture_viewer<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::picture_viewer<mux::app::actions>>() noexcept;
