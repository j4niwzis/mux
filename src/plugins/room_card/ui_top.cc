// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.room_card.ui_top -- A room not joined, as a link names it, in
// the person card's layout: its photo beside its name and how many are in
// it, what it is about, its ID to copy, and a button to join it. What its
// server says (/room_summary) fills it when it comes; till then, or where it
// says nothing, the address alone. An invite's: Accept and Decline.
module;
#include "skiff/named.h"
export module mux.plugin.room_card.ui_top;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.motion;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.ui.messages;
import mux.ui.cards;
export import mux.plugin.room_card.types;

export namespace mux::plugins::room_card {
using namespace ::mux::ui;

inline auto room_cover(const palette& colours, const std::string& key, const std::string& name, const std::string& line) {
  return SKIFF_NAMED(room_cover, skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}}),
      avatar_mark(avatar_of::chat{}, key, name, 72.0f),
      two_lines(colours, name, line, 17.0f, 6.0f, two_line_style{.selectable = true})));
}
using room_cover_t = decltype(room_cover(std::declval<const palette&>(), "", "", ""));

struct join_act {
  using Answer = std::variant<room_card::knock, room_card::join>;
  bool knocking = false;  // asked to be let in, where it lets people knock
  Answer operator()() const {
    if (knocking)
      return room_card::knock{};
    return room_card::join{};
  }
};
// Its name, else its address, else what the link said.
inline std::string room_card_name(const std::string& asked, const room_preview& known) {
  return !known.name.empty() ? known.name : !known.alias.empty() ? known.alias : asked;
}
// Under the name: the address, where the name is not it, and how many.
inline std::string room_card_line(const std::string& asked, const room_preview& known) {
  std::string out = known.alias.empty() ? (known.name.empty() ? std::string() : asked) : known.alias;
  if (out == room_card_name(asked, known))
    out.clear();
  if (known.members)
    out += std::format("{}{} {}", out.empty() ? "" : " · ", *known.members, *known.members == 1 ? "member" : "members");
  return out.empty() ? std::string("Room") : out;
}
// The details can be taller than the window. Only this column scrolls;
// closing the card and accepting or declining an invite stay in reach.
inline auto room_card_details(const palette& colours, const std::string& asked, const room_preview& known) {
  namespace c = skiff::compose;
  const std::string& key = known.id.empty() ? asked : known.id;
  nodes::Text about(
      !known.topic.empty()  ? known.topic
      : !known.note.empty() ? known.note
                            : std::string("No description"),
      14.0f, known.topic.empty() ? colours.dim : colours.text);
  about.setSelectable(true);
  return SKIFF_NAMED(room_card_details, c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      room_cover(colours, key, room_card_name(asked, known), room_card_line(asked, known)),
      section_band(colours),
      c::styled({.fillX = true, .margin = {2.0f, 22.0f, 8.0f, 22.0f}}, wrapped(std::move(about))),
      id_line(colours, key, "")));
}
inline auto card_of(const palette& colours, const std::string& asked, const room_preview& known) {
  namespace c = skiff::compose;
  using close_act = sends<request::close_shown<shown>>;
  using decline_act = sends<room_card::decline>;
  auto details = room_card_details(colours, asked, known);
  const scene::Spec tile_spec{.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}};
  return c::arranged<card_fit>({}, card_box(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}}),
      page_header<no_back, close_act>(colours, known.invite ? "Invite" : "Room info", {}, {}, false, true),
      c::styled({.fillX = true}, nodes::ScrollContainer<decltype(details)>(std::move(details))),
      c::styled(tile_spec, action_tile<join_act>(
          colours, known.invite ? "Accept" : known.knock ? "Ask to join" : "Join",
          icon::plus{}, {known.knock && !known.invite})),
      c::visible(known.invite, c::styled(tile_spec,
          action_tile<decline_act>(colours, "Decline", icon::close{}, {})))));
}
using box = decltype(card_of(std::declval<const palette&>(), std::string(), std::declval<const room_preview&>()));
// tdesktop's profile layer: 392 wide, as high as what it shows, near the top.
inline dialog_look content_look(std::type_identity<shown>) {
  return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}};
}
inline auto make_content(const ui_needs& n, const shown& facts) { return card_of(*n.colours, facts.asked, facts.known); }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::room_card
