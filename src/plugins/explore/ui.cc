// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.explore.ui -- Explore rooms, as Cinny's explorer: the listed
// account's server's directory, or a space's rooms, searched; each room
// joined, a space opened in turn. And its button in the side's head.
module;
#include "skiff/named.h"
export module mux.plugin.explore.ui;

import std;
import splice;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import mux.composition;
import mux.core;
import mux.protocols;
import mux.logic.text;
import mux.ui.base;
export import mux.plugin.explore.types;

export namespace mux::plugins::explore {
using namespace ::mux::ui;

// Its button in the side's head (mux.ui:conversations_side).
inline auto extend(contract::side_header, type_tag<feature>, const palette& colours) {
  return SKIFF_NAMED(explore_button, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                                            icon_button(colours, icon::compass{}, sends<open>{})));
}

// Explore its rooms, in a space's menu in the side.
struct explore_act {
  using Answer = request::explore_space;
  std::string room;
  Answer operator()() const { return Answer{room}; }
};
constexpr std::string_view label(contract::space_menu, type_tag<feature>) { return "Explore its rooms\u2026"; }
inline explore_act act(contract::space_menu, type_tag<feature>, std::string room) { return {std::move(room)}; }

// A room listed: its picture, name, address and members, topic; Join -- or,
// a space, Open.
struct row_act {
  using Answer = std::variant<request::explore_space, join>;
  std::string room, server;
  bool open = false;
  std::string name;
  std::optional<account_id> by;
  Answer operator()() const {
    if (open) return request::explore_space{room, name};
    return join{room, server, by};
  }
};
auto room_row(const palette& colours, const directory_room& one, const std::string& server, std::optional<account_id> by = std::nullopt) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(explore_room_row, c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 16.0f, 8.0f, 16.0f}}),
      face_spot(avatar_of::chat{}, one.id, one.name.empty() ? one.alias : one.name, 40.0f),
      c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX,
                              .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
          c::styled({.fillX = true}, elided(nodes::Text(
              one.name.empty() ? (one.alias.empty() ? one.id : one.alias) : one.name, 14.0f, colours.text, true))),
          c::styled({.fillX = true}, elided(nodes::Text(
              one.members > 0 ? std::format("{} · {} members", one.alias, one.members) : one.alias, 12.0f, colours.dim))),
          c::visible(!one.topic.empty(), c::styled({.fillX = true}, elided(nodes::Text(one.topic, 13.0f, colours.text))))),
      c::styled({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle},
          skiff::compose::primary(widgets::Button<row_act>(colours.widgets, one.space ? "Open" : "Join",
              {one.space ? one.id : (one.alias.empty() ? one.id : one.alias), server, one.space, one.name, by})))));
}
struct explore_draft { std::string query, server; };
struct submit_explore {};
struct explore_events {
  auto on(submit_explore, const explore_draft& draft) const {
    return skiff::model::Up{request::search_rooms{draft.server, draft.query}};
  }
};
auto explore_view(const palette& colours, const shown& facts) {
  namespace c = skiff::compose;
  const auto wanted = mux::logic::folded(facts.query);
  const auto rooms = facts.listed ? facts.listed->rooms | std::views::filter([&](const auto& room) {
    return !facts.space || wanted.empty() || mux::logic::folded(room.name).contains(wanted) ||
        mux::logic::folded(room.topic).contains(wanted) || mux::logic::folded(room.alias).contains(wanted);
  }) | std::ranges::to<std::vector>() : std::vector<directory_room>{};
  for (const auto& room : rooms) {
    if (room.avatar && !room.avatar->empty()) listed_avatars().emplace_back(room.id, *room.avatar);
  }
  auto rows = rooms | std::views::transform([&](const auto& room) {
    return room_row(colours, room, facts.listed->server, facts.by);
  }) | std::ranges::to<std::vector>();
  const bool more = facts.listed && facts.listed->next && !facts.space;
  const auto status = facts.loading ? std::string("Loading rooms…") :
      !facts.listed ? std::string() : rooms.empty() ? std::string("No rooms found.") : std::format("{} rooms", rooms.size());
  return SKIFF_NAMED(explore_view, c::local<explore_draft>(explore_events{}, c::column(
      c::vbox(0.0f, {.fill = true, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
      page_header<no_back, sends<request::close_shown<shown>>>(colours, "Explore rooms", {}, {}, false, true),
      c::visible(facts.space.has_value(), c::styled({.fillX = true, .margin = {0.0f, 10.0f, 8.0f, 10.0f}},
          nodes::Text(facts.space ? facts.space->name : std::string(), 17.0f, colours.text, true))),
      // A flow that wraps: the query its own line where the server does not
      // fit beside it -- a phone's dialog, 306 wide, left the query 6 of
      // them, its caption a letter a line (a tester's) -- the server and
      // Search under it then.
      c::styled({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 6.0f, 0.0f, 6.0f}}, nodes::Flow(
          nodes::FlowOptions{.direction = nodes::direction::horizontal{}, .spacingX = 8.0f, .spacingY = 8.0f, .wrap = true},
          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .minWidth = 150.0f},
              model_field<&explore_draft::query>(colours, "Find a room",
                  std::string(facts.by ? proto::directory_hint(facts.by->speaks) : std::string_view("Name, topic, or address")))),
          c::visible(!facts.space, c::styled({.width = 170.0f, .relativeSize = scene::axes::kNone},
              model_field<&explore_draft::server>(colours, "Server", facts.own_server))),
          c::styled({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd},
              skiff::compose::primary(widgets::SendButton<submit_explore>(colours.widgets, "Search", {}))))),
      c::visible(!status.empty(), c::styled({.fillX = true, .margin = {6.0f, 10.0f, 4.0f, 10.0f}},
          wrapped(nodes::Text(status, 13.0f, colours.dim)))),
      c::styled({.fillX = true, .grow = scene::axes::kY}, c::scrolled(
          c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)))),
      c::visible(more, c::styled({.fillX = true, .height = 32.0f,
                                .margin = {6.0f, 10.0f, 0.0f, 10.0f}, .disabled = facts.loading},
          widgets::SendButton<request::search_rooms>(colours.widgets, "Load more",
              {facts.listed ? facts.listed->server : facts.server,
               facts.listed ? facts.listed->query : facts.query,
               more ? facts.listed->next : std::nullopt})))), explore_draft{facts.query, facts.server}));
}
using box = decltype(explore_view(std::declval<const palette&>(), std::declval<const shown&>()));
inline auto make_content(const ui_needs& needs, const shown& facts) { return explore_view(*needs.colours, facts); }
inline dialog_look content_look(std::type_identity<shown>) { return {.size = dialog_size::fixed{640.0f, 560.0f}}; }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::explore
