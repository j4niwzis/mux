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
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import mux.core;
import mux.logic.text;
import mux.logic.links;
import mux.ui.base;
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
      avatar_mark(avatar_of::chat{}, one.id, one.name.empty() ? one.alias : one.name, 40.0f),
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
struct explore_listing {
  std::vector<directory_room> rooms;
  std::string server;
  std::optional<std::string> space;
  std::string query;
  std::optional<std::string> next;
  friend bool operator==(const explore_listing&, const explore_listing&) = default;
};
struct explore_space_shown {
  std::string room, name;
  friend bool operator==(const explore_space_shown&, const explore_space_shown&) = default;
};
struct explore_facts {
  std::string own_server;
  std::optional<explore_listing> listing;
  bool loading = false;
  std::optional<explore_space_shown> space;
  std::optional<account_id> by;
  // The outstanding search identifies which replies belong to this view.
  std::string server, query;
  friend bool operator==(const explore_facts&, const explore_facts&) = default;
};
// Compared: written again unchanged, it is not made again (show).
static_assert(std::equality_comparable<explore_facts>);
inline bool accepts_listing(const explore_facts& now, const change::directory_listed& listed) {
  return now.by == std::optional(listed.by) &&
      (now.space ? std::optional(now.space->room) : std::nullopt) == listed.space &&
      (listed.space || (now.server == listed.server && now.query == listed.query));
}
struct explore_draft { std::string query, server; };
struct submit_explore {};
struct explore_events {
  auto on(submit_explore, const explore_draft& draft) const {
    return skiff::model::Up{request::search_rooms{draft.server, draft.query}};
  }
};
auto explore_view(const palette& colours, const explore_facts& facts) {
  namespace c = skiff::compose;
  const auto wanted = mux::logic::folded(facts.query);
  const auto rooms = facts.listing ? facts.listing->rooms | std::views::filter([&](const auto& room) {
    return !facts.space || wanted.empty() || mux::logic::folded(room.name).contains(wanted) ||
        mux::logic::folded(room.topic).contains(wanted) || mux::logic::folded(room.alias).contains(wanted);
  }) | std::ranges::to<std::vector>() : std::vector<directory_room>{};
  for (const auto& room : rooms) {
    if (room.avatar && !room.avatar->empty()) listed_avatars().emplace_back(room.id, *room.avatar);
  }
  auto rows = rooms | std::views::transform([&](const auto& room) {
    return directory_room_row(colours, room, facts.listing->server, facts.by);
  }) | std::ranges::to<std::vector>();
  const bool more = facts.listing && facts.listing->next && !facts.space;
  const auto status = facts.loading ? std::string("Loading rooms…") :
      !facts.listing ? std::string() : rooms.empty() ? std::string("No rooms found.") : std::format("{} rooms", rooms.size());
  return SKIFF_NAMED(explore_view, c::local<explore_draft>(explore_events{}, c::column(
      c::vbox(0.0f, {.fill = true, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
      page_header<no_back, sends<request::close_explore>>(colours, "Explore rooms", {}, {}, false, true),
      c::visible(facts.space.has_value(), c::styled({.fillX = true, .margin = {0.0f, 10.0f, 8.0f, 10.0f}},
          nodes::Text(facts.space ? facts.space->name : std::string(), 17.0f, colours.text, true))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 6.0f, 0.0f, 6.0f}}),
          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              model_field<&explore_draft::query>(colours, "Find a room",
                  std::string(facts.by ? proto::directory_hint(facts.by->speaks) : std::string_view("Name, topic, or address")))),
          c::visible(!facts.space, c::styled({.width = 170.0f, .relativeSize = scene::axes::kNone},
              model_field<&explore_draft::server>(colours, "Server", facts.own_server))),
          c::styled({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd},
              primary(widgets::SendButton<submit_explore>(colours.widgets, "Search", {})))),
      c::visible(!status.empty(), c::styled({.fillX = true, .margin = {6.0f, 10.0f, 4.0f, 10.0f}},
          wrapped(nodes::Text(status, 13.0f, colours.dim)))),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(
          c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)))),
      c::visible(more, c::styled({.fillX = true, .height = 32.0f,
                                .margin = {6.0f, 10.0f, 0.0f, 10.0f}, .disabled = facts.loading},
          widgets::SendButton<request::search_rooms>(colours.widgets, "Load more",
              {facts.listing ? facts.listing->server : facts.server,
               facts.listing ? facts.listing->query : facts.query,
               more ? facts.listing->next : std::nullopt})))), explore_draft{facts.query, facts.server}));
}
using explore_box = decltype(explore_view(std::declval<const palette&>(), std::declval<const explore_facts&>()));
inline auto make_content(const ui_needs& needs, const explore_facts& facts) {
  return explore_view(*needs.colours, facts);
}
inline dialog_look content_look(std::type_identity<explore_facts>) {
  return {.size = dialog_size::fixed{640.0f, 560.0f}};
}
}  // namespace mux::ui

