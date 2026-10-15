// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:drawer -- The drawer.
export module mux.ui:drawer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.ui.base;
import :accounts;

export namespace mux::ui {

// ---- the drawer -------------------------------------------------------------------

// An account in the drawer, as the program says: its address, its protocol
// and how it is (in red where it failed), and whether its chats are the
// ones listed.
struct drawer_account {
  std::string address;
  std::string protocol;
  std::string how;
  bool failed = false;
  bool current = false;
  friend bool operator==(const drawer_account&, const drawer_account&) = default;
};
struct drawer_accounts {
  std::vector<drawer_account> accounts;
  friend bool operator==(const drawer_accounts&, const drawer_accounts&) = default;
};
// Its row: a round avatar with its initials, its address over its protocol
// and state, a tick on the current one -- lit; pressed, it is made the
// current account, whose chats are listed.
inline auto drawer_account_row(const palette& colours, const drawer_account& one) {
  namespace c = skiff::compose;
  auto row = c::onPress(sends<request::switch_account>{{one.address}},
      c::row(c::hbox(14.0f, {.fillX = true, .height = 56.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = colours.chosen,
                             .selectedBackground = colours.chosen, .focusBackground = colours.chosen, .selected = one.current}),
             avatar_mark(avatar_of::person{}, one.address, one.address, 38.0f),
             two_lines(colours, one.address, std::format("{} · {}", one.protocol, one.how), 14.0f, 3.0f,
                       two_line_style{.second_ink = one.failed ? colours.error : colours.dim, .show_second = true}),
             c::visible(one.current, c::styled({.alignSelf = scene::align::kMiddle}, icon_mark(colours, icon::check{})))),
      one.address);
  return row;
}
struct drawer_account_row_of {
  const palette* colours;
  auto operator()(const drawer_account& one) const { return drawer_account_row(*colours, one); }
};
// What the drawer holds, as Telegram's does: the accounts with Manage
// accounts under them, then a new chat, a new room, Settings and Quit, each
// a full-width line with its icon.
inline auto drawer_view(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  return c::column(c::vbox(0.0f, {.fill = true}),
      c::styled({.margin = {18.0f, 20.0f, 14.0f, 20.0f}}, nodes::Text("mux", 20.0f, colours.text, true)),
      c::projected<drawer_accounts>([](const drawer_accounts& now) { return now.accounts; },
          c::listed<drawer_account>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), drawer_account_row_of{&colours})),
      c::styled({.margin = {0.0f, 0.0f, 6.0f, 0.0f}}, row_item(colours, "Manage accounts", sends<request::open_accounts>{}, icon::person{})),
      c::styled({.fillX = true, .height = 1.0f, .margin = {0.0f, 0.0f, 6.0f, 0.0f}}, nodes::Box<>(colours.chosen)),
      row_item(colours, "Start new chat", sends<request::open_new_chat>{}, icon::person{}),
      row_item(colours, "New room", sends<request::open_new_room>{}, icon::people{}),
      row_item(colours, "Settings", sends<request::open_settings>{}, icon::gear{}),
      row_item(colours, "Quit", sends<request::quit>{}, icon::power{}));
}
using drawer_view_t = decltype(drawer_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::drawer_view_t> = true;  // walks_drawer.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::drawer_view_t>() noexcept;
