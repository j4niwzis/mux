// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.forward.ui -- Forward, in a message's menu and for the messages
// selected; and where to, as tdesktop's box: the account's chats, with a
// field to find one by its name -- a press sends them there.
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.forward.ui;

import std;
import mux.logic.text;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
import skiff.widgets.textbox;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.plugin.forward.types;

export namespace mux::plugins::forward {
using namespace ::mux::ui;

// Forward, in a message's menu where the account forwards (mux.ui:context_menu).
MUX_INLINE_IN_RELEASE auto extend(contract::message_menu, type_tag<feature>, const palette& colours, const emoji_kept&, const menu_facts& facts) {
  return skiff::compose::visible(facts.can.forward,
      row_item(colours, "Forward", sends<show>{show{facts.id}}, icon::send{}, std::nullopt, menu_row_look));
}
// Offered for the messages selected where their account forwards.
inline bool allowed(contract::selection_actions, type_tag<feature>, const ui_shared& shared, const conversation_id& chat) {
  return ops_of(shared, chat.account).forward;
}

struct draft {
  std::string query;
};
struct draft_events {};
MUX_INLINE_IN_RELEASE auto target_row(const palette& colours, const target& one) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(target_row, c::onPress(
      sends<to>{to{one.id}},
      c::row(c::hbox(12.0f, {.fillX = true,
                             .height = 50.0f,
                             .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                             .hoverBackground = colours.chosen}),
             face_spot(avatar_of::chat{}, one.id.id, one.name, 36.0f),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                       elided(nodes::Text(one.name, 15.0f, colours.text)))),
      one.name));
}
// The chats whose names have what is typed, in any case.
inline std::vector<target> matching(const std::vector<target>& all, std::string_view text) {
  const std::string wanted = mux::logic::folded(text);
  return all | std::views::filter([&](const target& one) {
    return wanted.empty() || mux::logic::folded(one.name).contains(wanted);
  }) | std::ranges::to<std::vector>();
}
MUX_INLINE_IN_RELEASE auto panel(const palette& colours, const std::vector<target>& chats) {
  namespace c = skiff::compose;
  using query = skiff::model::Field<&draft::query>;
  auto field = widgets::TextField<std::string>(colours.widgets, "Search");
  field.setSearchIcon(true);
  return SKIFF_NAMED(panel, c::local<draft>(
      draft_events{},
      c::column(
          c::vbox(0.0f, {.fillX = true, .height = 520.0f}),
          page_header<no_back, sends<request::close_shown<shown>>>(colours, "Forward to…", {}, {}, false, true),
          c::styled({.fillX = true, .height = 34.0f, .margin = {0.0f, 16.0f, 8.0f, 16.0f}},
                    c::bound<query>(std::move(field))),
          c::styled({.fillX = true, .grow = scene::axes::kY},
                    c::scrolled(
                        c::projected<query>(
                            [all = chats](const std::string& text) { return matching(all, text); },
                            c::listed<target>(
                                c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                [colours = &colours](const target& one) { return target_row(*colours, one); }))))),
      draft{}));
}
using box = decltype(panel(std::declval<const palette&>(), std::declval<const std::vector<target>&>()));
MUX_INLINE_IN_RELEASE auto make_content(const ui_needs& n, const shown& facts) {
  return panel(*n.colours, facts.chats);
}
inline dialog_look content_look(std::type_identity<shown>) {
  return {.size = dialog_size::fixed{400.0f, 520.0f}};
}

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::forward
