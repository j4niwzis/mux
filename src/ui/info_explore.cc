// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_explore -- The selected account's directory, or a space's rooms.
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
import :base;
import :avatars;
import :controls;
import :themes;
import :forms;
export namespace mux::ui {
inline std::optional<std::string> room_address_for(const std::optional<account_id>& by, std::string_view query) {
  if (!by) return std::nullopt;
  if (by->speaks == protocol_t(protocol::xmpp{})) {
    const auto room = proto::xmpp::room_address(query);
    return room ? std::optional(room->jid) : std::nullopt;
  }
  auto link = mux::logic::link_of_id(query);
  if (!link) link = mux::logic::link_of(query);
  if (!link) return std::nullopt;
  return spl::visit(spl::overloaded{
      [](const proto::matrix::link::room& room) { return std::optional(room.id); },
      [](const auto&) { return std::optional<std::string>(); }}, *link);
}
template <class Actions> struct directory_join {
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
template <class Actions>
auto directory_room_row(const palette& colours, const directory_room& one, const std::string& server, std::optional<account_id> by = std::nullopt) {
  namespace c = skiff::compose;
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 16.0f, 8.0f, 16.0f}}),
      avatar_mark(one.id, one.name.empty() ? one.alias : one.name, 40.0f),
      c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX,
                              .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
          c::styled({.fillX = true}, elided(nodes::Text(
              one.name.empty() ? (one.alias.empty() ? one.id : one.alias) : one.name, 14.0f, colours.text, true))),
          c::styled({.fillX = true}, elided(nodes::Text(
              one.members > 0 ? std::format("{} · {} members", one.alias, one.members) : one.alias, 12.0f, colours.dim))),
          c::visible(!one.topic.empty(), c::styled({.fillX = true}, elided(nodes::Text(one.topic, 13.0f, colours.text))))),
      c::styled({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle},
          primary(widgets::Button<directory_join<Actions>>(colours.widgets, one.space ? "Open" : "Join",
              {one.space ? one.id : (one.alias.empty() ? one.id : one.alias), server, one.space, one.name, by}))));
}
template <class Actions>
using directory_row = decltype(directory_room_row<Actions>(std::declval<const palette&>(),
    std::declval<const directory_room&>(), std::declval<const std::string&>()));
struct explore_listing {
  std::vector<directory_room> rooms;
  std::string server;
  std::optional<std::string> space;
  std::string query;
  std::optional<std::string> next;
};
struct explore_space_shown { std::string room, name; };
struct explore_facts {
  std::string own_server;
  std::optional<explore_listing> listing;
  bool loading = false;
  std::optional<explore_space_shown> space;
  std::optional<account_id> by;
  // The outstanding search identifies which replies belong to this view.
  std::string server, query;
};
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
template <class Actions>
auto explore_view(const palette& colours, const explore_facts& facts) {
  namespace c = skiff::compose;
  const auto wanted = mux::logic::folded(facts.query);
  const auto rooms = facts.listing ? std::ranges::to<std::vector>(std::views::filter(facts.listing->rooms, [&](const auto& room) {
    return !facts.space || wanted.empty() || mux::logic::folded(room.name).contains(wanted) ||
        mux::logic::folded(room.topic).contains(wanted) || mux::logic::folded(room.alias).contains(wanted);
  })) : std::vector<directory_room>{};
  std::ranges::for_each(rooms, [](const auto& room) {
    if (room.avatar && !room.avatar->empty()) listed_avatars().emplace_back(room.id, *room.avatar);
  });
  auto rows = std::ranges::to<std::vector>(std::views::transform(rooms, [&](const auto& room) {
    return directory_room_row<Actions>(colours, room, facts.listing->server, facts.by);
  }));
  const bool more = facts.listing && facts.listing->next && !facts.space;
  const auto status = facts.loading ? std::string("Loading rooms…") :
      !facts.listing ? std::string() : rooms.empty() ? std::string("No rooms found.") : std::format("{} rooms", rooms.size());
  return c::local<explore_draft>(explore_events{}, c::column(
      c::vbox(0.0f, {.fill = true, .padding = {0.0f, 12.0f, 12.0f, 12.0f}}),
      page_header<no_back, sends<request::close_explore>>(colours, "Explore rooms", {}, {}, false, true),
      c::visible(facts.space.has_value(), c::styled({.fillX = true, .margin = {0.0f, 10.0f, 8.0f, 10.0f}},
          nodes::Text(facts.space ? facts.space->name : std::string(), 17.0f, colours.text, true))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 6.0f, 0.0f, 6.0f}}),
          c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              model_field<&explore_draft::query>(colours, "Find a room",
                  facts.by && facts.by->speaks == protocol_t(protocol::xmpp{}) ? "Name or room@conference.example.org" : "Name, topic, or address")),
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
               more ? facts.listing->next : std::nullopt})))), explore_draft{facts.query, facts.server});
}
template <class Actions>
using explore_box = decltype(explore_view<Actions>(std::declval<const palette&>(), std::declval<const explore_facts&>()));
template <class Content, class Actions>
auto make_content(std::type_identity<Content>, const ui_needs<Actions>& needs, const explore_facts& facts) {
  return explore_view<Actions>(*needs.colours, facts);
}
template <class Content>
inline dialog_look content_look(std::type_identity<Content>, std::type_identity<explore_facts>) {
  return {.size = dialog_size::fixed{640.0f, 560.0f}};
}
}  // namespace mux::ui
