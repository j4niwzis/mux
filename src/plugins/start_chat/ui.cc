// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.start_chat.ui -- Element's Start chat (its InviteDialog, for a
// direct chat): who to talk to, found as it is typed -- among those one
// already has chats with, and in the server's user directory -- each with
// their picture, name and ID, a press on one starting the chat; and one's
// own link, to send to someone not found. Inviting to a room, the same.
// And its row in the drawer.
module;
#include "skiff/named.h"
export module mux.plugin.start_chat.ui;

import std;
import splice;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.bind;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.textbox;
import mux.composition;
import mux.core;
import mux.logic.text;
import mux.protocols;
import mux.ui.base;
export import mux.plugin.start_chat.types;

export namespace mux::plugins::start_chat {
using namespace ::mux::ui;

// Its row in the drawer (mux.ui:drawer).
inline auto extend(contract::drawer_rows, type_tag<feature>, const palette& colours) {
  return row_item(colours, "Start new chat", sends<request::open_new_chat>{}, icon::person{});
}

using choose_person_request = pick;
inline choose_person_request choose_person(const std::optional<conversation_id>&, std::string user) { return pick{std::move(user)}; }
inline bool direct_address(const shown& facts, std::string_view text) {
  if (!facts.by || !proto::owns_address(state_before(facts.by->speaks), text)) return false;
  return text.starts_with('@') ? text.find(':') != std::string_view::npos : text.find('@') != std::string_view::npos;
}
inline auto people_rows(const shown& facts) {
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
inline std::optional<choose_person_request> start_chat_request(const shown& now) {
  if (direct_address(now, now.query)) return choose_person(now.inviting, now.query);
  if (!now.rows.empty()) return choose_person(now.inviting, (*now.rows.begin()).first);
  return std::nullopt;
}
inline auto person_rows(const palette& colours, const shown& facts) {
  return facts.rows | std::views::transform([&](const auto& row) {
    return person_pick_row(colours, row.second, choose_person(facts.inviting, row.second.id));
  }) | std::ranges::to<std::vector>();
}
using person_rows_t = decltype(skiff::compose::many(skiff::compose::vbox(),
    person_rows(std::declval<const palette&>(), std::declval<const shown&>())));
inline auto start_chat_view(const palette& colours, const shown& facts) {
  namespace c = skiff::compose;
  const auto words = proto::direct_chat_form_of(facts.by ? state_before(facts.by->speaks) : state_before(protocol_t{}));
  return SKIFF_NAMED(start_chat_view, c::scoped<shown>(start_chat_events{facts.by, facts.inviting}, c::column(
      c::vbox(8.0f, {.fill = true, .padding = {0.0f, 12.0f, 16.0f, 12.0f}}),
      page_header<no_back, sends<request::close_shown<shown>>>(colours, facts.inviting ? "Invite to " + facts.room_name : "Start chat", {}, {}, false, true),
      c::styled({.fillX = true}, wrapped(nodes::Text(facts.inviting ? "Choose a contact or enter their full address to invite them to this room." : words.intro, 14.0f, colours.text))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              widgets::TextBox<find_people_typed>(colours.widgets, words.hint, {})),
          c::styled({.width = 64.0f, .height = 34.0f},
              c::projected<shown>(start_chat_request,
                  skiff::compose::primary(widgets::SendButton<std::optional<choose_person_request>>(
                      colours.widgets, facts.inviting ? "Invite" : "Go", start_chat_request(facts)))))),
      c::text_for<shown>([](const auto& now) {
        return now.query.empty() ? std::string("Suggestions") : now.rows.empty() ? std::string("No results") : std::string("Results");
      }, nodes::Text("", 12.0f, colours.dim, true)),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(
          c::projected<shown>([](const auto& now) { return std::optional(now); },
              c::mount<person_rows_t, shown>([colours = &colours](const auto& now) {
                return std::tuple{c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), person_rows(*colours, now))};
              }, {.fillX = true, .autoSize = scene::axes::kY})))),
      c::visible(!facts.inviting, c::styled({.fillX = true}, wrapped(nodes::Text("You can share your address with someone you haven't chatted with yet.", 13.0f, colours.dim)))),
      c::visible(!facts.inviting, nodes::Text(facts.own_link, 13.0f, colours.accent)),
      c::visible(!facts.inviting, widgets::SendButton<request::copy_text>(colours.widgets, "Copy link", {facts.own_link})))));
}
using box = decltype(start_chat_view(std::declval<const palette&>(), std::declval<const shown&>()));
inline bool content_persistent(std::type_identity<shown>) { return true; }
inline bool content_matches(const shown& was, const shown& now) { return was.by == now.by && was.inviting == now.inviting; }
inline dialog_look content_look(std::type_identity<shown>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
inline auto make_content(const ui_needs& needs, const shown& facts) { return start_chat_view(*needs.colours, facts); }

// Its dialog, among the window's (mux.ui:window).
constexpr type_tag<box> extend(contract::window_dialogs, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::start_chat
