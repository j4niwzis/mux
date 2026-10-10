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
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;
import :composer;
import :message;
import :html;
import :timeline;  // a message's menu, for the reactions list's bubbles
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
template <class Actions> struct forward_box : skiff::compose::Stacked {
  // Child references and handlers require a fixed address.
  forward_box(const forward_box&) = delete;
  forward_box& operator=(const forward_box&) = delete;
  forward_box(forward_box&&) = delete;
  forward_box& operator=(forward_box&&) = delete;

  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{400.0f, 520.0f}}; }
  // The colours it is made in, for the rows it makes later.
  const palette* colours_ = nullptr;
  std::vector<forward_target> all;
  struct close_it {
    using Answer = ::mux::ui::request::close_forward;
    ::mux::ui::request::close_forward operator()() { return ::mux::ui::request::close_forward{}; }
  };
  struct typed {
    forward_box* box;
    void operator()(std::string_view text) const { box->find(text); }
  };
  struct row : skiff::compose::Stacked {
    // What its handlers ask for, returned.
    using Answer = ::mux::ui::request::forward_to;
    conversation_id id;
    struct parts_t {
      avatar_mark face;
      nodes::Text name;
    } parts;
    row(const palette &colours, const forward_target &one)
        : Stacked(skiff::compose::hbox(12.0f,
                                       {.fillX = true,
                                        .height = 50.0f,
                                        .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                                        .hoverBackground = colours.chosen})),
          id(one.id),
          parts{
              .face = avatar_mark(one.id.id, one.name, 36.0f),
              .name = skiff::compose::styled(
                  {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text(one.name, 15.0f, colours.text)))} {

      fState.setCursor(scene::cursor::hand{});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    std::optional<Answer> onClick(float, float) {
      return ::mux::ui::request::forward_to{id};
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  using header_t = page_header_t<no_back, close_it>;
  struct parts_t {
    header_t header;
    widgets::TextBox<typed> field;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  forward_box(const ui_needs<Actions>& n, const forward_facts& facts) : forward_box(*n.colours, facts.chats) {}
  forward_box(const palette &colours, const std::vector<forward_target> &chats)
      : Stacked(skiff::compose::vbox(0.0f, {.fillX = true, .height = 520.0f})),
        colours_(&colours), all(chats),
        parts{.header = page_header<no_back, close_it>(colours, "Forward to…", {}, {}, false, true),
              .field = skiff::compose::styled(
                  {.fillX = true,
                   .height = 34.0f,
                   .margin = {0.0f, 16.0f, 8.0f, 16.0f}},
                  widgets::TextBox<typed>(colours.widgets, "Search", {this}))} {
    parts.field.setSearchIcon(true);
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    this->find({});
  }
  // The chats whose names have what is typed, in any case.
  void find(std::string_view text) {
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(text);
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    for (const forward_target& one : all)
      if (wanted.empty() || lower(one.name).contains(wanted))
        rows.emplace_back(*colours_, one);
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
};

// Someone found: their picture, name and ID; pressed, the chat with them --
// in Start chat, and in the chat list where nothing joined matches.
template <class Event>
inline auto person_pick_row(const palette& colours, const found_person& one, Event event) {
  const std::string name = one.name.empty() ? one.id : one.name;
  return skiff::compose::onClick(std::move(event), skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = 52.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f},
          .cornerRadius = 8.0f, .hoverBackground = colours.chosen}),
      avatar_mark(one.id, name, 36.0f),
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
  std::ranges::for_each(std::views::filter(facts.people, [&](const auto& person) {
    return wanted.empty() || mux::logic::folded(person.name).contains(wanted) || mux::logic::folded(person.id).contains(wanted);
  }), [&](const auto& person) { if (rows.size() < 60) rows.put(person.id, person); });
  std::ranges::for_each(facts.found, [&](const auto& person) { if (rows.size() < 60) rows.put(person.id, person); });
  return rows;
}
struct find_people_typed {
  using Answer = request::find_people;
  Answer operator()(std::string_view text) const { return {std::string(text)}; }
};
struct start_chat_go {};
struct start_chat_events {
  std::optional<account_id> by;
  std::optional<conversation_id> inviting;
  auto on(const request::find_people& event, const new_chat_facts& now) const {
    auto next = now;
    next.query = event.query;
    next.found.clear();
    next.rows = people_rows(next);
    return std::tuple{skiff::model::over<new_chat_facts>(skiff::model::setTo(std::move(next))), skiff::model::Up{event}};
  }
  auto on(start_chat_go, const new_chat_facts& now) const {
    std::optional<choose_person_request> asked;
    if (direct_address(now, now.query)) asked = choose_person(now.inviting, now.query);
    else if (!now.rows.empty()) asked = choose_person(now.inviting, (*now.rows.begin()).first);
    return skiff::model::Up{asked};
  }
};
inline auto start_chat_view(const palette& colours, const new_chat_facts& facts) {
  namespace c = skiff::compose;
  const auto words = proto::direct_chat_form_of(facts.by ? state_before(facts.by->speaks) : state_before(protocol::matrix{}));
  return c::scoped<new_chat_facts>(start_chat_events{facts.by, facts.inviting}, c::column(
      c::vbox(8.0f, {.fillX = true, .height = 560.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}}),
      page_header<no_back, sends<request::close_new_chat>>(colours, facts.inviting ? "Invite to " + facts.room_name : "Start chat", {}, {}, false, true),
      c::styled({.fillX = true}, wrapped(nodes::Text(facts.inviting ? "Choose a contact or enter their full address to invite them to this room." : words.intro, 14.0f, colours.text))),
      c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.height = 36.0f, .grow = scene::axes::kX},
              widgets::TextBox<find_people_typed>(colours.widgets, words.hint, {})),
          c::styled({.width = 64.0f, .height = 34.0f},
              primary(widgets::SendButton<start_chat_go>(colours.widgets, facts.inviting ? "Invite" : "Go", {})))),
      c::text_for<new_chat_facts>([](const auto& now) {
        return now.query.empty() ? std::string("Suggestions") : now.rows.empty() ? std::string("No results") : std::string("Results");
      }, nodes::Text("", 12.0f, colours.dim, true)),
      c::styled({.fillX = true, .grow = scene::axes::kY}, nodes::ScrollContainer(
          c::each<std::string, found_person>([colours = &colours, room = facts.inviting](const auto& person) {
            return person_pick_row(*colours, person, choose_person(room, person.id));
          }, c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY})))),
      c::visible(!facts.inviting, c::styled({.fillX = true}, wrapped(nodes::Text("You can share your address with someone you haven't chatted with yet.", 13.0f, colours.dim)))),
      c::visible(!facts.inviting, nodes::Text(facts.own_link, 13.0f, colours.accent)),
      c::visible(!facts.inviting, widgets::SendButton<request::copy_text>(colours.widgets, "Copy link", {facts.own_link}))));
}
using start_chat_box_t = decltype(start_chat_view(std::declval<const palette&>(), std::declval<const new_chat_facts&>()));
template <class Actions> using start_chat_box = start_chat_box_t;
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
inline auto create_room_view(const palette& colours, const new_room_facts& facts) {
  namespace c = skiff::compose;
  using access = skiff::model::Field<&room_draft::access>;
  const auto form = proto::room_creation_form_of(facts.by ? state_before(facts.by->speaks) : state_before(protocol::matrix{}), facts.own_server);
  return c::local<room_draft>(room_creation_events{facts, form}, c::column(
      c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 22.0f, 18.0f, 22.0f}}),
      page_header<no_back, sends<request::close_new_room>>(colours,
          facts.place ? std::string(facts.place->make_space ? "Create a space in " : "Create a room in ") + facts.place->name : "Create a room", {}, {}, false, true),
      model_field<&room_draft::name>(colours, "Name", ""),
      model_field<&room_draft::topic>(colours, "Topic (optional)", ""),
      nodes::Text("Who can join", 13.0f, colours.dim),
      c::bound<access>(widgets::ChoiceRowField<room_access_t>(colours.widgets, "Invite only", room_access::invited{})),
      c::bound<access>(widgets::ChoiceRowField<room_access_t>(colours.widgets, "Anyone", room_access::everyone{})),
      c::visible(facts.place.has_value(), c::bound<access>(widgets::ChoiceRowField<room_access_t>(
          colours.widgets, "Space members", room_access::space{}))),
      c::shown_for<access>([required = form.address_required](const auto& access) {
        return required || access == room_access_t(room_access::everyone{});
      }, model_field<&room_draft::address>(colours, "Address", form.address_hint)),
      c::visible(form.encryption, c::shown_for<access>([](const auto& access) {
        return access != room_access_t(room_access::everyone{});
      }, room_form_toggle<&room_draft::encrypted>(colours, "Enable end-to-end encryption"))),
      c::visible(form.federation, room_form_toggle<&room_draft::advanced>(colours, "Advanced")),
      c::visible(form.federation, c::shown_for<skiff::model::Field<&room_draft::advanced>>([](bool shown) { return shown; },
          room_form_toggle<&room_draft::federate>(colours, "Allow people from other servers"))),
      c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), nodes::justify::end{}),
          widgets::SendButton<request::close_new_room>(colours.widgets, "Cancel", {}),
          primary(widgets::SendButton<submit_room>(colours.widgets, facts.place && facts.place->make_space ? "Create space" : "Create room", {})))),
      room_draft{.access = facts.place ? room_access_t(room_access::space{}) : room_access_t(room_access::invited{}),
                 .encrypted = form.encryption});
}
using create_room_box_t = decltype(create_room_view(std::declval<const palette&>(), std::declval<const new_room_facts&>()));
template <class Actions> using create_room_box = create_room_box_t;
inline dialog_look content_look(std::type_identity<create_room_box_t>) { return {.size = dialog_size::fitting{480.0f}}; }
template <class Needs>
auto make_content(std::type_identity<create_room_box_t>, const Needs& needs, const new_room_facts& facts) {
  return create_room_view(*needs.colours, facts);
}
}  // namespace mux::ui
