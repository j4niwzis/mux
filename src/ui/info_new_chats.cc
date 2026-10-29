// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_new_chats -- Start chat; Create a room.
module;
#include "skiff/named.h"
export module mux.ui:info_new_chats;

import std;
import mux.logic.text;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
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
import :info_common;
import :info_cards;

export namespace mux::ui {
// Someone found: their picture, name and ID; pressed, the chat with them --
// in Start chat, and in the chat list where nothing joined matches.
template <class Event>
inline auto person_pick_row(const palette& colours, const found_person& one, Event event) {
  const std::string name = one.name.empty() ? one.id : one.name;
  return SKIFF_NAMED(person_pick_row, skiff::compose::onClick(std::move(event), skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = 52.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f},
          .cornerRadius = 8.0f, .hoverBackground = colours.chosen}),
      avatar_mark(avatar_of::person{}, one.id, name, 36.0f),
      two_lines(colours, name, one.id, 14.0f, 2.0f)), name));
}
inline auto found_person_row(const palette& colours, const found_person& one) {
  return person_pick_row(colours, one, request::start_direct{one.id});
}
using found_person_row_t = decltype(found_person_row(std::declval<const palette&>(), std::declval<const found_person&>()));

// Element's Start chat (its InviteDialog, for a direct chat): who to talk
// to, found as it is typed -- among those one already has chats with, and
// in the server's user directory -- each with their picture, name and ID,
// a press on one starting the chat; and one's own link, to send to someone
// not found.
// Start chat open: the people known, and one's own link.
struct new_chat_facts {
  std::vector<found_person> people;
  std::string own_link;
  std::vector<found_person> found;
  std::string query;
  std::optional<account_id> by;
  skiff::model::Keyed<std::string, found_person> rows;
  std::optional<conversation_id> inviting;
  std::string room_name;
};
using choose_person_request = spl::variant<request::start_direct, request::invite_to_room>;
inline choose_person_request choose_person(const std::optional<conversation_id>& room, std::string user) {
  if (room) return request::invite_to_room{*room, std::move(user)};
  return request::start_direct{std::move(user)};
}
inline bool direct_address(const new_chat_facts& facts, std::string_view text) {
  if (!facts.by || !proto::owns_address(state_before(facts.by->speaks), text)) return false;
  return text.starts_with('@') ? text.find(':') != std::string_view::npos : text.find('@') != std::string_view::npos;
}
inline auto people_rows(const new_chat_facts& facts) {
  skiff::model::Keyed<std::string, found_person> rows;
  const auto wanted = mux::logic::folded(facts.query);
  if (direct_address(facts, facts.query)) rows.put(facts.query, found_person{.id = facts.query});
  for (const auto& person : facts.people | std::views::filter([&](const auto& person) {
    return wanted.empty() || mux::logic::folded(person.name).contains(wanted) || mux::logic::folded(person.id).contains(wanted);
  })) { if (rows.size() < 60) rows.put(person.id, person); }
  for (const auto& person : facts.found) { if (rows.size() < 60) rows.put(person.id, person); }
  return rows;
}
struct find_people_typed {
  using Answer = request::find_people;
  Answer operator()(std::string_view text) const { return {std::string(text)}; }
};
struct start_chat_events {
  std::optional<account_id> by;
  std::optional<conversation_id> inviting;
};
inline std::optional<choose_person_request> start_chat_request(const new_chat_facts& now) {
  if (direct_address(now, now.query)) return choose_person(now.inviting, now.query);
  if (!now.rows.empty()) return choose_person(now.inviting, (*now.rows.begin()).first);
  return std::nullopt;
}
inline auto person_rows(const palette& colours, const new_chat_facts& facts) {
  return facts.rows | std::views::transform([&](const auto& row) {
    return person_pick_row(colours, row.second, choose_person(facts.inviting, row.second.id));
  }) | std::ranges::to<std::vector>();
}
using person_rows_t = decltype(skiff::compose::many(skiff::compose::vbox(),
    person_rows(std::declval<const palette&>(), std::declval<const new_chat_facts&>())));
inline auto start_chat_view(const palette& colours, const new_chat_facts& facts) {
  namespace c = skiff::compose;
  const auto words = proto::direct_chat_form_of(facts.by ? state_before(facts.by->speaks) : state_before(protocol_t{}));
  return SKIFF_NAMED(start_chat_view, c::scoped<new_chat_facts>(start_chat_events{facts.by, facts.inviting}, c::column(
      c::vbox(8.0f, {.fill = true, .padding = {0.0f, 12.0f, 16.0f, 12.0f}}),
      page_header<no_back, sends<request::close_new_chat>>(colours, facts.inviting ? "Invite to " + facts.room_name : "Start chat", {}, {}, false, true),
      c::styled({.fillX = true}, wrapped(nodes::Text(facts.inviting ? "Choose a contact or enter their full address to invite them to this room." : words.intro, 14.0f, colours.text))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              widgets::TextBox<find_people_typed>(colours.widgets, words.hint, {})),
          c::styled({.width = 64.0f, .height = 34.0f},
              c::projected<new_chat_facts>(start_chat_request,
                  primary(widgets::SendButton<std::optional<choose_person_request>>(
                      colours.widgets, facts.inviting ? "Invite" : "Go", start_chat_request(facts)))))),
      c::text_for<new_chat_facts>([](const auto& now) {
        return now.query.empty() ? std::string("Suggestions") : now.rows.empty() ? std::string("No results") : std::string("Results");
      }, nodes::Text("", 12.0f, colours.dim, true)),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(
          c::projected<new_chat_facts>([](const auto& now) { return std::optional(now); },
              c::mount<person_rows_t, new_chat_facts>([colours = &colours](const auto& now) {
                return std::tuple{c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), person_rows(*colours, now))};
              }, {.fillX = true, .autoSize = scene::axes::kY})))),
      c::visible(!facts.inviting, c::styled({.fillX = true}, wrapped(nodes::Text("You can share your address with someone you haven't chatted with yet.", 13.0f, colours.dim)))),
      c::visible(!facts.inviting, nodes::Text(facts.own_link, 13.0f, colours.accent)),
      c::visible(!facts.inviting, widgets::SendButton<request::copy_text>(colours.widgets, "Copy link", {facts.own_link})))));
}
using start_chat_box_t = decltype(start_chat_view(std::declval<const palette&>(), std::declval<const new_chat_facts&>()));
using start_chat_box = start_chat_box_t;
inline bool content_persistent(std::type_identity<new_chat_facts>) { return true; }
inline bool content_matches(const new_chat_facts& was, const new_chat_facts& now) {
  return was.by == now.by && was.inviting == now.inviting;
}
inline dialog_look content_look(std::type_identity<new_chat_facts>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Needs>
auto make_content(const Needs& needs, const new_chat_facts& facts) {
  return start_chat_view(*needs.colours, facts);
}

}  // namespace mux::ui

