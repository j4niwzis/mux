// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info -- A chat's info, and a person's.
module;
#include "skiff/named.h"
export module mux.ui:info;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.ui.base;
import mux.ui.messages;

export import mux.ui.cards;
export import :info_cards;
export import :info_looks;
export import :info_explore;

export namespace mux::ui {

// A chat's info, as Telegram's profile: beside the chat, in one column that
// scrolls -- its big avatar, name and how it is, its tiles (mute, Manage,
// leave), what it is about and its addresses, its ID; then its members, a
// part at a time. All of it as the program says (info_shown, the chat
// view's part); a press asks the program.

// The chat's tiles: mute or unmute it, Manage, and Leave where its
// protocol lets it be left.
inline auto info_tiles(const palette& colours, bool muted, bool leavable) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(info_tiles, c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}}),
      c::styled({.grow = scene::axes::kX}, action_tile<sends<request::toggle_mute>>(colours, muted ? "Unmute" : "Mute", icon::bell{})),
      c::styled({.grow = scene::axes::kX}, action_tile<sends<request::open_manage>>(colours, "Manage", icon::sliders{})),
      c::visible(leavable, c::styled({.grow = scene::axes::kX}, action_tile<sends<request::leave_chat>>(colours, "Leave", icon::leave{})))));
}
// What the upper part shows.
struct info_view {
  std::string key;
  std::string name;
  std::string status;
  bool group = false;
  bool muted = false;
  // What copying the ID gives: for a Matrix room, a link to it with the
  // servers to join through; else the ID.
  std::string copied;
  // What it is about, and the addresses it publishes: shown as
  // Telegram shows a group's description and its link.
  std::string topic;
  std::vector<std::string> addresses;
  // Whether its protocol lets it be left: its Leave tile only then.
  bool leavable = true;
  friend bool operator==(const info_view&, const info_view&) = default;
};
// The upper part, made from its view: ✕; the big avatar, the name, how it
// is; the tiles; what it is about, its addresses; its ID.
inline auto info_head(const palette& colours, const info_view& shown) {
  namespace c = skiff::compose;
  const auto centred = [&](std::string text, float size, skia::SkColor colour, bool bold) {
    auto node = nodes::Text(std::move(text), size, colour, bold);
    node.setSelectable(true);
    return c::styled({.alignSelf = scene::align::kMiddle, .margin = {4.0f, 20.0f, 0.0f, 20.0f}}, elided(std::move(node)));
  };
  auto description = wrapped(c::styled({.fillX = true}, nodes::Text(shown.topic, 14.0f, colours.text)));
  description.setSelectable(true);
  description.setLinks(link_spans_in(shown.topic), colours.accent);
  return SKIFF_NAMED(info_head, c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::justified(c::hbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 0.0f, 8.0f}}), nodes::justify::end{}),
                   icon_button(colours, icon::close{}, sends<request::toggle_info>{})),
      c::styled({.alignSelf = scene::align::kMiddle}, avatar_button(avatar_of::chat{}, shown.key, shown.name, 96.0f)),
      centred(shown.name, 17.0f, colours.text, true),
      centred(shown.status, 13.0f, colours.dim, false),
      info_tiles(colours, shown.muted, shown.leavable),
      section_band(colours),
      c::visible(!shown.topic.empty(), c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}}),
                                                 std::move(description), nodes::Text("Description", 12.0f, colours.dim))),
      c::many(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
              shown.addresses | std::views::transform([&](const std::string& address) { return id_line(colours, address, "", "Address"); }) | std::ranges::to<std::vector>()),
      id_line(colours, shown.key, shown.copied)));
}
using info_head_t = decltype(info_head(std::declval<const palette&>(), std::declval<const info_view&>()));
// The members' head: how many, and a way to invite where one may.
struct info_members_heading {
  std::size_t count = 0;
  conversation_id room;
  bool invite = false;
  friend bool operator==(const info_members_heading&, const info_members_heading&) = default;
};
inline auto info_members_head(const palette& colours, const info_members_heading& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(info_members_head, c::row(c::hbox(10.0f, {.fillX = true, .height = 48.0f, .padding = {6.0f, 10.0f, 6.0f, 16.0f}}),
      icon_view(colours, icon::people{}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                nodes::Text(std::format("{} MEMBER{}", facts.count, facts.count == 1 ? "" : "S"), 13.0f, colours.dim, true)),
      c::visible(facts.invite, icon_button(colours, icon::add_person{}, sends<request::open_room_invite>{{facts.room}}))));
}
using info_members_head_t = decltype(info_members_head(std::declval<const palette&>(), std::declval<const info_members_heading&>()));
// A member as the list shows them: who, with their role where the chat's
// protocol says one; and how they are, with what it says of them.
struct info_member {
  member who;
  std::string how;
  friend bool operator==(const info_member&, const info_member&) = default;
};
// A file the chat holds, as its list shows it: its message, when, and what
// it carries.
struct info_file {
  std::string id;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  attachment carried;
  friend bool operator==(const info_file&, const info_file&) = default;
};
// The info, as the program says: the chat's view, its members' head, and
// the members made so far -- the first, and more as the list nears their
// end -- and whether there are more; and its files, newest first.
struct info_shown {
  info_view head;
  info_members_heading heading;
  std::vector<info_member> members;
  bool more = false;
  std::vector<info_file> files;
  friend bool operator==(const info_shown&, const info_shown&) = default;
};
// A file's row: the one a message shows it with, pressed to open it.
struct info_file_row {
  const palette* colours = nullptr;
  auto operator()(const std::tuple<std::string, info_file>& item) const {
    const attachment& carried = std::get<1>(item).carried;
    return file_view(*colours, carried.source, carried.name, carried.size, false,
                     file_open{request::open_file{carried.source, carried.name}});
  }
};
using info_file_rows_t = nodes::MemoRows<std::string, info_file, file_view_t, info_file_row>;
// A member's row, kept while it shows the same.
struct info_member_row {
  const palette* colours = nullptr;
  auto operator()(const std::tuple<std::string, info_member>& item) const {
    const info_member& one = std::get<1>(item);
    return member_row(*colours, one.who, one.how);
  }
};
using info_member_rows_t = nodes::MemoRows<std::string, info_member, member_row_t, info_member_row>;

// The panel: its column, and its left edge. Near the end of the members
// made, with more, it asks for the next few (request::info_more).
inline auto info_panel(const ui_needs& n) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const auto listed = [](const info_shown& now) { return now.head.group; };
  // Filling the column the screen sizes for it, as the threads' panel does.
  return SKIFF_NAMED(info_panel, c::column(c::vbox(0.0f, {.fill = true, .background = colours.sidebar, .masking = true}),
      c::styled({.fillX = true, .grow = scene::axes::kY},
          c::fetching_for<info_shown, conversation_id>([](const info_shown& now) {
            return c::Fetched<conversation_id>{now.heading.room, now.members.size(), now.more};
          },
              sends<request::info_more>{},
              nodes::ScrollContainer(c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::projected<info_shown>([](const info_shown& now) { return std::optional(now.head); },
                      c::mount<info_head_t, info_view>([at = &colours](const info_view& view) { return std::tuple{info_head(*at, view)}; },
                                                       {.fillX = true, .autoSize = scene::axes::kY})),
                  // Its files, as Element X's Files: what it holds that is
                  // a file -- not to go to another client for them (a
                  // tester's).
                  c::shown_for<info_shown>([](const info_shown& now) { return !now.files.empty(); },
                      c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                          section_band(colours),
                          c::text_for<info_shown>([](const info_shown& now) {
                            return std::format("{} FILE{}", now.files.size(), now.files.size() == 1 ? "" : "S");
                          },
                              c::styled({.margin = {12.0f, 16.0f, 4.0f, 16.0f}}, nodes::Text("", 13.0f, colours.dim, true))),
                          c::projected<info_shown>([](const info_shown& now) {
                            return now.files | std::views::transform([](const info_file& one) { return std::tuple{one.id, one}; }) | std::ranges::to<std::vector>();
                          },
                              c::styled({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 16.0f, 8.0f, 16.0f}},
                                        info_file_rows_t(info_file_row{&colours}))))),
                  c::shown_for<info_shown>(listed, c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                      section_band(colours),
                      c::projected<info_shown>([](const info_shown& now) { return std::optional(now.heading); },
                          c::mount<info_members_head_t, info_members_heading>(
                              [at = &colours](const info_members_heading& facts) { return std::tuple{info_members_head(*at, facts)}; },
                              {.fillX = true, .height = 48.0f})),
                      c::projected<info_shown>([](const info_shown& now) {
                        return now.members | std::views::transform([](const info_member& one) { return std::tuple{one.who.id, one}; }) | std::ranges::to<std::vector>();
                      },
                          c::styled({.fillX = true, .autoSize = scene::axes::kY}, info_member_rows_t(info_member_row{&colours}))))))))),
      c::styled({.place = scene::anchor::kTopLeft, .fillY = true, .width = 1.0f}, nodes::Box<>(colours.band))));
}
using info_panel_t = decltype(info_panel(std::declval<const ui_needs&>()));
// Its width beside the chat.
inline constexpr float kInfoWidth = 340.0f;

}  // namespace mux::ui

