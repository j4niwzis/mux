// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_explore -- The selected account's directory, or a space's rooms.
module;
#include "skiff/named.h"
export module mux.ui:info_explore;
import std;
import splice;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import mux.core;
import mux.logic.text;
import mux.logic.links;
import mux.ui.base;
// The plugins' faces (contract::faces), seen where a room's is put.
import mux.plugins.ui;
export namespace mux::ui {
// The room an address typed names: one its protocol joins at once, else the
// room a link to it names.
inline std::optional<std::string> room_address_for(const std::optional<account_id>& by, std::string_view query) {
  if (!by) return std::nullopt;
  if (auto room = proto::directory_room(by->speaks, query)) return room;
  auto link = mux::logic::link_of_id(query);
  if (!link) link = mux::logic::link_of(query);
  return link ? mux::logic::linked_room(*link) : std::nullopt;
}
struct directory_join {
  using Answer = std::variant<request::explore_space, request::join_directory_room>;
  std::string room, server;
  bool open = false;
  std::string name;
  std::optional<account_id> by;
  Answer operator()() const {
    if (open) return request::explore_space{room, name};
    return request::join_directory_room{room, server, by};
  }
};
auto directory_room_row(const palette& colours, const directory_room& one, const std::string& server, std::optional<account_id> by = std::nullopt) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(directory_room_row, c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 16.0f, 8.0f, 16.0f}}),
      face_of(colours, avatar_of::chat{}, one.id, one.name.empty() ? one.alias : one.name, 40.0f),
      c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX,
                              .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
          c::styled({.fillX = true}, elided(nodes::Text(
              one.name.empty() ? (one.alias.empty() ? one.id : one.alias) : one.name, 14.0f, colours.text, true))),
          c::styled({.fillX = true}, elided(nodes::Text(
              one.members > 0 ? std::format("{} · {} members", one.alias, one.members) : one.alias, 12.0f, colours.dim))),
          c::visible(!one.topic.empty(), c::styled({.fillX = true}, elided(nodes::Text(one.topic, 13.0f, colours.text))))),
      c::styled({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle},
          primary(widgets::Button<directory_join>(colours.widgets, one.space ? "Open" : "Join",
              {one.space ? one.id : (one.alias.empty() ? one.id : one.alias), server, one.space, one.name, by})))));
}
using directory_row = decltype(directory_room_row(std::declval<const palette&>(),
    std::declval<const directory_room&>(), std::declval<const std::string&>()));
}  // namespace mux::ui

