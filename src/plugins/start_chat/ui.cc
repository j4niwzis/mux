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
// Whether what is typed is a whole address of the account's protocol.
inline bool direct_address(const account_id& by, std::string_view text) {
  if (!proto::owns_address(state_before(by.speaks), text)) return false;
  return text.starts_with('@') ? text.find(':') != std::string_view::npos : text.find('@') != std::string_view::npos;
}
template <class Facts>
auto people_rows(const Facts& facts) {
  skiff::model::Keyed<std::string, found_person> rows;
  const auto wanted = mux::logic::folded(facts.query);
  if (direct_address(facts.by, facts.query)) rows.put(facts.query, found_person{.id = facts.query});
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
struct start_chat_events {};
template <class Facts>
std::optional<choose_person_request> start_chat_request(const Facts& now) {
  if (direct_address(now.by, now.query)) return pick{now.query};
  if (!now.rows.empty()) return pick{(*now.rows.begin()).first};
  return std::nullopt;
}
template <class Facts>
auto person_rows(const palette& colours, const Facts& facts) {
  return facts.rows | std::views::transform([&](const auto& row) {
    return person_pick_row(colours, row.second, pick{row.second.id});
  }) | std::ranges::to<std::vector>();
}
template <class Facts>
using person_rows_t = decltype(skiff::compose::many(skiff::compose::vbox(),
    person_rows(std::declval<const palette&>(), std::declval<const Facts&>())));
// What differs by the dialog's kind, made only where it is had: the words
// over it and on its button; and, starting a chat, one's own link to send.
template <class Protocol>
std::string title_of(const start_of<Protocol>&) { return "Start chat"; }
template <class Protocol>
std::string title_of(const invite_of<Protocol>& facts) { return "Invite to " + facts.room_name; }
template <class Protocol>
std::string intro_of(const start_of<Protocol>&, const proto::chat_form& words) { return words.intro; }
template <class Protocol>
std::string intro_of(const invite_of<Protocol>&, const proto::chat_form&) {
  return "Choose a contact or enter their full address to invite them to this room.";
}
template <class Protocol>
std::string button_of(const start_of<Protocol>&) { return "Go"; }
template <class Protocol>
std::string button_of(const invite_of<Protocol>&) { return "Invite"; }
template <class Protocol>
auto own_link_part(const palette&, const invite_of<Protocol>&) { return nothing_shown(); }
template <class Protocol>
auto own_link_part(const palette& colours, const start_of<Protocol>& facts) {
  namespace c = skiff::compose;
  return c::column(c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.fillX = true}, wrapped(nodes::Text("You can share your address with someone you haven't chatted with yet.", 13.0f, colours.dim))),
      nodes::Text(facts.own_link, 13.0f, colours.accent),
      widgets::SendButton<request::copy_text>(colours.widgets, "Copy link", {facts.own_link}));
}
template <class Facts>
auto start_chat_view(const palette& colours, const Facts& facts) {
  namespace c = skiff::compose;
  const auto words = proto::direct_chat_form_of(state_before(facts.by.speaks));
  return SKIFF_NAMED(start_chat_view, c::scoped<Facts>(start_chat_events{}, c::column(
      c::vbox(8.0f, {.fill = true, .padding = {0.0f, 12.0f, 16.0f, 12.0f}}),
      page_header<no_back, sends<request::close_shown<Facts>>>(colours, title_of(facts), {}, {}, false, true),
      c::styled({.fillX = true}, wrapped(nodes::Text(intro_of(facts, words), 14.0f, colours.text))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
              widgets::TextBox<find_people_typed>(colours.widgets, words.hint, {})),
          c::styled({.width = 64.0f, .height = 34.0f},
              c::projected<Facts>([](const Facts& now) { return start_chat_request(now); },
                  skiff::compose::primary(widgets::SendButton<std::optional<choose_person_request>>(
                      colours.widgets, button_of(facts), start_chat_request(facts)))))),
      c::text_for<Facts>([](const auto& now) {
        return now.query.empty() ? std::string("Suggestions") : now.rows.empty() ? std::string("No results") : std::string("Results");
      }, nodes::Text("", 12.0f, colours.dim, true)),
      // Scrolled by the combinator: its rows are builders (their text pieces).
      c::styled({.fillX = true, .grow = scene::axes::kY}, c::scrolled(
          c::projected<Facts>([](const auto& now) { return std::optional(now); },
              c::mount<person_rows_t<Facts>, Facts>([colours = &colours](const auto& now) {
                return std::tuple{c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), person_rows(*colours, now))};
              }, {.fillX = true, .autoSize = scene::axes::kY})))),
      own_link_part(colours, facts))));
}
template <class Protocol>
bool content_persistent(std::type_identity<start_of<Protocol>>) { return true; }
template <class Protocol>
bool content_persistent(std::type_identity<invite_of<Protocol>>) { return true; }
template <class Protocol>
bool content_matches(const start_of<Protocol>& was, const start_of<Protocol>& now) { return was.by == now.by; }
template <class Protocol>
bool content_matches(const invite_of<Protocol>& was, const invite_of<Protocol>& now) { return was.by == now.by && was.inviting == now.inviting; }
template <class Protocol>
dialog_look content_look(std::type_identity<start_of<Protocol>>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Protocol>
dialog_look content_look(std::type_identity<invite_of<Protocol>>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Protocol>
auto make_content(const ui_needs& needs, const start_of<Protocol>& facts) { return start_chat_view(*needs.colours, facts); }
template <class Protocol>
auto make_content(const ui_needs& needs, const invite_of<Protocol>& facts) { return start_chat_view(*needs.colours, facts); }

// Its dialogs, among the window's (mux.ui:window): starting a chat and
// inviting, one each for each protocol.
template <class>
struct dialogs_of;
template <class... Protocols>
struct dialogs_of<protocol_list<Protocols...>> {
  using type = composition::page_list<start_of<Protocols>..., invite_of<Protocols>...>;
  using requests = proto::request_list<request::close_shown<start_of<Protocols>>..., request::close_shown<invite_of<Protocols>>...>;
  // Each open one, given to `f` -- one at most is.
  template <class Showing, class F>
  static void each_open(Showing& showing, F f) {
    (with<start_of<Protocols>>(showing, f), ...);
    (with<invite_of<Protocols>>(showing, f), ...);
  }
  template <class Facts, class Showing, class F>
  static void with(Showing& showing, F& f) {
    if (const auto& open = *showing.template look<std::optional<Facts>>())
      f(*open);
  }
  template <class Showing>
  static void close(Showing& showing) {
    // show() is the window's (mux.ui), found by ADL on the model.
    (show(showing, std::optional<start_of<Protocols>>{}), ...);
    (show(showing, std::optional<invite_of<Protocols>>{}), ...);
  }
};
using dialogs = dialogs_of<protocols>;
constexpr typename dialogs::type facts_types(contract::window_dialogs, type_tag<feature>) { return {}; }
template <class Facts>
constexpr auto extend(contract::window_dialogs, type_tag<feature>, type_tag<Facts>) {
  return type_tag<decltype(start_chat_view(std::declval<const palette&>(), std::declval<const Facts&>()))>{};
}

}  // namespace mux::plugins::start_chat
