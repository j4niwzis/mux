// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:drawer -- The drawer.
module;
#include "skiff/named.h"
export module mux.ui:drawer;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.ui.base;
// Its rows' text, pieces the plugins shape where they are put (a::part).
import mux.ui.lists;
import mux.ui.shaping;
import mux.plugins.ui;
import :accounts;

export namespace mux::ui {

// ---- the drawer -------------------------------------------------------------------

// An account in the drawer, as the program says: its address, its protocol
// and how it is (in red where it failed), and whether its chats are the
// ones listed.
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
             // Its text, a piece the plugins shape where it is put (the avatars
             // plugin's picture beside it).
             a::part(drawer_account_text, colours, one),
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
// The plugins' rows in the drawer (contract::drawer_rows), in their order --
// Start chat and New room, for two.
template <class List>
struct drawer_row_list;
template <class... Features>
struct drawer_row_list<plugin_list<Features...>> {
  template <class Feature>
  static auto row_of(const palette& colours) -> decltype(extend(contract::drawer_rows{}, type_tag<Feature>{}, colours)) {
    static_assert(requires { extend(contract::drawer_rows{}, type_tag<Feature>{}, colours); },
                  composition::broken<contract::drawer_rows, Feature>());
    return extend(contract::drawer_rows{}, type_tag<Feature>{}, colours);
  }
  static auto rows(const palette& colours) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row_of<Features>(colours)...);
  }
};
using drawer_plugins = drawer_row_list<composition::providers<contract::drawer_rows, enabled_plugins>>;
inline auto drawer_view(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  // Under its title, all of it scrolled where the window is lower than it
  // -- a phone's in split screen, 300 high, had Settings and Quit below its
  // edge.
  return SKIFF_NAMED(drawer_view, c::column(c::vbox(0.0f, {.fill = true}),
      c::styled({.margin = {18.0f, 20.0f, 14.0f, 20.0f}}, nodes::Text("mux", 20.0f, colours.text, true)),
      c::styled({.fillX = true, .grow = scene::axes::kY},
          nodes::ScrollContainer(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              c::projected<drawer_accounts>([](const drawer_accounts& now) { return now.accounts; },
                  c::listed<drawer_account>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), drawer_account_row_of{&colours})),
              c::styled({.margin = {0.0f, 0.0f, 6.0f, 0.0f}}, row_item(colours, "Manage accounts", sends<request::open_accounts>{}, icon::person{})),
              c::styled({.fillX = true, .height = 1.0f, .margin = {0.0f, 0.0f, 6.0f, 0.0f}}, nodes::Box<>(colours.chosen)),
              drawer_plugins::rows(colours),
              row_item(colours, "Settings", sends<request::open_settings>{}, icon::gear{}),
              row_item(colours, "Quit", sends<request::quit>{}, icon::power{}))))));
}
using drawer_view_t = decltype(drawer_view(std::declval<const ui_needs&>()));

}  // namespace mux::ui

