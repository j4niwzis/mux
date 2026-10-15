// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_new_chats -- Where a message is forwarded to; Start chat; Create a room.
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
import :info_reactions;

export namespace mux::ui {
// A room made in a space, as its menu asks: the space, its name, and
// whether what is made is a space.
struct new_room_place {
  conversation_id space;
  std::string name;
  bool make_space = false;
};

// A chat to forward to: its id and name.
struct forward_target {
  conversation_id id;
  std::string name;
};

// Where to forward a message, as tdesktop's box: the account's chats, with
// a field to find one by its name; a press sends it there.
// Where a message may be forwarded to: the chats asked among.
struct forward_facts {
  std::vector<forward_target> chats;
};
struct forward_draft {
  std::string query;
};
struct forward_events {};
inline auto forward_row(const palette& colours, const forward_target& one) {
  namespace c = skiff::compose;
  return c::onPress(
      sends<::mux::ui::request::forward_to>{one.id},
      c::row(c::hbox(12.0f, {.fillX = true,
                             .height = 50.0f,
                             .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                             .hoverBackground = colours.chosen}),
             avatar_mark(avatar_of::chat{}, one.id.id, one.name, 36.0f),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                       elided(nodes::Text(one.name, 15.0f, colours.text)))),
      one.name);
}
// The chats whose names have what is typed, in any case.
inline std::vector<forward_target> matching_forward_targets(const std::vector<forward_target>& all, std::string_view text) {
  const std::string wanted = mux::logic::folded(text);
  return all | std::views::filter([&](const forward_target& one) {
    return wanted.empty() || mux::logic::folded(one.name).contains(wanted);
  }) | std::ranges::to<std::vector>();
}
inline auto forward_view(const palette& colours, const std::vector<forward_target>& chats) {
  namespace c = skiff::compose;
  using close_act = sends<::mux::ui::request::close_forward>;
  using query = skiff::model::Field<&forward_draft::query>;
  auto field = widgets::TextField<std::string>(colours.widgets, "Search");
  field.setSearchIcon(true);
  return c::local<forward_draft>(
      forward_events{},
      c::column(
          c::vbox(0.0f, {.fillX = true, .height = 520.0f}),
          page_header<no_back, close_act>(colours, "Forward to…", {}, {}, false, true),
          c::styled({.fillX = true, .height = 34.0f, .margin = {0.0f, 16.0f, 8.0f, 16.0f}},
                    c::bound<query>(std::move(field))),
          c::styled({.fillX = true, .grow = scene::axes::kY},
                    nodes::ScrollContainer(
                        c::projected<query>(
                            [all = chats](const std::string& text) {
                              return matching_forward_targets(all, text);
                            },
                            c::listed<forward_target>(
                                c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                [colours = &colours](const forward_target& one) {
                                  return forward_row(*colours, one);
                                }))))),
      forward_draft{});
}
inline auto forward_view(const ui_needs& n, const forward_facts& facts) {
  return forward_view(*n.colours, facts.chats);
}
using forward_box_t = decltype(forward_view(std::declval<const ui_needs&>(), std::declval<const forward_facts&>()));
using forward_box = forward_box_t;
inline dialog_look content_look(std::type_identity<forward_box_t>) {
  return {.size = dialog_size::fixed{400.0f, 520.0f}};
}
template <class Needs>
auto make_content(std::type_identity<forward_box_t>, const Needs& needs, const forward_facts& facts) {
  return forward_view(needs, facts);
}

// Someone found: their picture, name and ID; pressed, the chat with them --
// in Start chat, and in the chat list where nothing joined matches.
template <class Event>
inline auto person_pick_row(const palette& colours, const found_person& one, Event event) {
  const std::string name = one.name.empty() ? one.id : one.name;
  return skiff::compose::onClick(std::move(event), skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = 52.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f},
          .cornerRadius = 8.0f, .hoverBackground = colours.chosen}),
      avatar_mark(avatar_of::person{}, one.id, name, 36.0f),
      two_lines(colours, name, one.id, 14.0f, 2.0f)), name);
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
  std::ranges::for_each(facts.people | std::views::filter([&](const auto& person) {
    return wanted.empty() || mux::logic::folded(person.name).contains(wanted) || mux::logic::folded(person.id).contains(wanted);
  }), [&](const auto& person) { if (rows.size() < 60) rows.put(person.id, person); });
  std::ranges::for_each(facts.found, [&](const auto& person) { if (rows.size() < 60) rows.put(person.id, person); });
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
  return c::scoped<new_chat_facts>(start_chat_events{facts.by, facts.inviting}, c::column(
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
                return std::tuple{c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), person_rows(*colours, now)};
              }, {.fillX = true, .autoSize = scene::axes::kY})))),
      c::visible(!facts.inviting, c::styled({.fillX = true}, wrapped(nodes::Text("You can share your address with someone you haven't chatted with yet.", 13.0f, colours.dim)))),
      c::visible(!facts.inviting, nodes::Text(facts.own_link, 13.0f, colours.accent)),
      c::visible(!facts.inviting, widgets::SendButton<request::copy_text>(colours.widgets, "Copy link", {facts.own_link}))));
}
using start_chat_box_t = decltype(start_chat_view(std::declval<const palette&>(), std::declval<const new_chat_facts&>()));
using start_chat_box = start_chat_box_t;
inline bool content_persistent(std::type_identity<start_chat_box_t>) { return true; }
inline bool content_matches(const start_chat_box_t& up, const new_chat_facts& facts) {
  return up.fHandlers.by == facts.by && up.fHandlers.inviting == facts.inviting;
}
inline dialog_look content_look(std::type_identity<start_chat_box_t>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Needs>
auto make_content(std::type_identity<start_chat_box_t>, const Needs& needs, const new_chat_facts& facts) {
  return start_chat_view(*needs.colours, facts);
}

// Element's Create a room (its CreateRoomDialog): a name, a topic, who can
// join -- by invitation, or anyone, with the address it is found by -- and,
// among the advanced, whether those of other servers may ever join.
// Create a room open: on which server, and where in a space, if in one.
struct new_room_facts {
  std::string own_server;
  std::optional<new_room_place> place;
  std::optional<account_id> by;
};
namespace room_access {
struct invited { friend bool operator==(invited, invited) = default; };
struct everyone { friend bool operator==(everyone, everyone) = default; };
struct space { friend bool operator==(space, space) = default; };
}
using room_access_t = spl::variant<room_access::invited, room_access::everyone, room_access::space>;
struct room_draft {
  std::string name, topic, address;
  room_access_t access = room_access::invited{};
  bool encrypted = true, federate = true, advanced = false;
};
struct submit_room {};
struct room_creation_events {
  new_room_facts facts;
  proto::creation_form form;
  auto on(submit_room, const room_draft& draft) const {
    std::optional<request::create_room> asked;
    const auto [open, members] = spl::visit(spl::overloaded{
        [](room_access::invited) { return std::pair{false, false}; },
        [](room_access::everyone) { return std::pair{true, false}; },
        [](room_access::space) { return std::pair{false, true}; }}, draft.access);
    if (!draft.name.empty() && (!form.address_required || !draft.address.empty()))
      asked = request::create_room{draft.name, draft.topic, open, draft.address, draft.federate,
          form.encryption && draft.encrypted && !open,
          facts.place ? std::optional(facts.place->space) : std::nullopt, members,
          facts.place && facts.place->make_space};
    return skiff::model::Up{asked};
  }
};
template <auto Member>
auto room_form_toggle(const palette& colours, std::string label) {
  namespace c = skiff::compose;
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.grow = scene::axes::kX}, wrapped(nodes::Text(label, 13.0f, colours.text))),
      c::bound<skiff::model::Field<Member>>(widgets::ToggleField<bool>(colours.widgets)));
}
// Radio rows send changes to their bound field; their mark is projected from it.
inline auto room_access_choice(const palette& colours, std::string label, room_access_t choice) {
  namespace c = skiff::compose;
  using access = skiff::model::Field<&room_draft::access>;
  return c::spec_for<access>([choice](const auto& selected) { return scene::Spec{.selected = selected == choice}; },
      c::onClick(skiff::bind::own(skiff::model::setTo(choice)),
          c::row(c::hbox(16.0f, {.fillX = true, .height = 46.0f,
                                .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                                .hoverBackground = colours.chosen}),
              c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  nodes::Text(label, 15.0f, colours.text)),
              c::styled({.width = 20.0f, .alignSelf = scene::align::kMiddle},
                  c::text_for<access>([choice](const auto& selected) {
                    return selected == choice ? std::string("●") : std::string("○");
                  }, nodes::Text("", 20.0f, colours.accent)))), label));
}
inline auto create_room_view(const palette& colours, const new_room_facts& facts) {
  namespace c = skiff::compose;
  using access = skiff::model::Field<&room_draft::access>;
  const auto form = proto::room_creation_form_of(facts.by ? state_before(facts.by->speaks) : state_before(protocol_t{}), facts.own_server);
  return c::local<room_draft>(room_creation_events{facts, form}, c::column(
      c::vbox(8.0f, {.fill = true, .padding = {0.0f, 22.0f, 18.0f, 22.0f}}),
      page_header<no_back, sends<request::close_new_room>>(colours,
          facts.place ? std::string(facts.place->make_space ? "Create a space in " : "Create a room in ") + facts.place->name : "Create a room", {}, {}, false, true),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(c::column(
          c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          model_field<&room_draft::name>(colours, "Name", ""),
          model_field<&room_draft::topic>(colours, "Topic (optional)", ""),
          nodes::Text("Who can join", 13.0f, colours.dim),
          room_access_choice(colours, "Invite only", room_access::invited{}),
          room_access_choice(colours, "Anyone", room_access::everyone{}),
          c::visible(facts.place.has_value(), room_access_choice(colours, "Space members", room_access::space{})),
          c::shown_for<access>([required = form.address_required](const auto& access) {
            return required || access == room_access_t(room_access::everyone{});
          }, model_field<&room_draft::address>(colours, "Address", form.address_hint)),
          c::visible(form.encryption, c::shown_for<access>([](const auto& access) {
            return access != room_access_t(room_access::everyone{});
          }, room_form_toggle<&room_draft::encrypted>(colours, "Enable end-to-end encryption"))),
          c::visible(form.federation, room_form_toggle<&room_draft::advanced>(colours, "Advanced")),
          c::visible(form.federation, c::shown_for<skiff::model::Field<&room_draft::advanced>>([](bool shown) { return shown; },
              room_form_toggle<&room_draft::federate>(colours, "Allow people from other servers")))))),
      c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::justify::end{}),
          c::styled({.width = 90.0f, .height = 34.0f}, widgets::SendButton<request::close_new_room>(colours.widgets, "Cancel", {})),
          c::styled({.width = 128.0f, .height = 34.0f},
              primary(widgets::SendButton<submit_room>(colours.widgets, facts.place && facts.place->make_space ? "Create space" : "Create room", {}))))),
      room_draft{.access = facts.place ? room_access_t(room_access::space{}) : room_access_t(room_access::invited{}),
                 .encrypted = form.encryption});
}
using create_room_box_t = decltype(create_room_view(std::declval<const palette&>(), std::declval<const new_room_facts&>()));
using create_room_box = create_room_box_t;
inline dialog_look content_look(std::type_identity<create_room_box_t>) { return {.size = dialog_size::fixed{480.0f, 560.0f}}; }
template <class Needs>
auto make_content(std::type_identity<create_room_box_t>, const Needs& needs, const new_room_facts& facts) {
  return create_room_view(*needs.colours, facts);
}
}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::start_chat_box> = true;  // walks_boxes.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::start_chat_box>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::create_room_box> = true;  // walks_boxes.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::create_room_box>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::forward_box> = true;  // walks_boxes.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::forward_box>() noexcept;
