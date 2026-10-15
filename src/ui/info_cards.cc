// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_cards -- A person's card, and a room's not joined.
export module mux.ui:info_cards;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
import :info_common;

export namespace mux::ui {
// A person's info, as tdesktop's profile layer: a box in the middle of the
// window over the chats -- a bar with its title and ✕, their photo beside
// their name and how they are, their ID to copy, and a message to them.
// A person's card open: from which account, who, and what is known of them.
struct person_shown {
  account_id account;
  std::string key;
  person_facts facts;
};
auto person_cover(const palette& colours, const std::string& key, const person_facts& facts) {
  return skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}}),
      avatar_button(avatar_of::person{}, key, facts.name, 72.0f),
      two_lines(colours, facts.name, facts.status, 17.0f, 6.0f, two_line_style{.selectable = true}));
}
using person_cover_t = decltype(person_cover(std::declval<const palette&>(), "", std::declval<const person_facts&>()));

inline nodes::Text profile_biography(const palette& colours, const body& biography) {
  auto text = wrapped(nodes::Text(biography.plain, 14.0f, colours.text));
  if (biography.html) {
    auto read = read_html(*biography.html);
    text.setText(std::move(read.text)); text.setStyles(std::move(read.styles), colours.accent);
    text.setLinks(std::move(read.spans), colours.accent);
  }
  text.apply({.fillX = true, .margin = {8.0f, 22.0f, 8.0f, 22.0f}});
  text.setSelectable(true);
  return text;
}
inline std::string profile_pronouns(const profile_details& profile) {
  return profile.pronouns | std::views::transform(&pronouns::summary) | std::views::join_with(std::string_view(" · ")) | std::ranges::to<std::string>();
}
inline palette profile_text_colours(palette colours, bool banner) {
  if (banner) {
    colours.text = skia::colorSetARGB(255, 255, 255, 255);
    colours.dim = skia::colorSetARGB(255, 235, 240, 243);
  }
  return colours;
}
inline auto profile_cover_details(const palette& colours, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  return c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      person_cover(colours, key, facts),
      c::visible(!facts.profile.pronouns.empty(), c::styled(
          {.fillX = true, .margin = {0.0f, 22.0f, 8.0f, 22.0f}},
          wrapped(nodes::Text(profile_pronouns(facts.profile), 13.0f, colours.dim)))),
      c::visible(!facts.profile.biography.plain.empty() || facts.profile.biography.html.has_value(),
          profile_biography(colours, facts.profile.biography)));
}
// The banner sits behind the name, pronouns and biography. Its background
// ends at the section divider, leaving the account ID and actions plain.
inline auto profile_cover(const palette& colours, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  const bool has_banner = facts.profile.banner.has_value();
  nodes::Image<from_moving_thumbnail> banner(from_moving_thumbnail{facts.profile.banner.value_or("")});
  banner.keepBox();
  return c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(has_banner, c::styled({.place = scene::anchor::kTopLeft, .fill = true}, std::move(banner))),
      c::visible(has_banner, c::styled({.place = scene::anchor::kTopLeft, .fill = true}, nodes::Box<>(skia::colorSetARGB(96, 0, 0, 0)))),
      profile_cover_details(profile_text_colours(colours, has_banner), key, facts));
}
using profile_cover_t = decltype(profile_cover(std::declval<const palette&>(), "", std::declval<const person_facts&>()));

// Measure a fitting card at its available width without losing the scroll
// anchor's previous position. Both user and room cards use this viewport.
inline float card_part_height(auto& node, const skia::SkRect& parent) {
  const auto previous = skiff::compose::state_of(node).fLastConstraint;
  scene::layout(node, skia::SkRect::MakeXYWH(previous.fLeft, previous.fTop, parent.width(), 0.0f));
  return node.bounds().height() + skiff::compose::state_of(node).fMargin.totalY();
}
// Keep a short card compact; for a long one, size the scroll viewport to the
// room the dialog gives the card rather than the content's unbounded height.
// What a card holds beside its head and its details: the person's card
// nothing; the room's its join button, and its decline where it is shown.
template <class Top, class Scroll>
float card_rest_height(std::tuple<Top, Scroll>&, const skia::SkRect&) {
  return 0.0f;
}
template <class Top, class Scroll, class Join, class Decline>
float card_rest_height(std::tuple<Top, Scroll, Join, Decline>& parts, const skia::SkRect& parent) {
  auto& decline = std::get<3>(parts);
  return card_part_height(std::get<2>(parts), parent) + (decline.visible() ? card_part_height(decline, parent) : 0.0f);
}
// A card: its head, its details scrolling, what it offers under them -- in
// their places, which card_fit names.
template <class... Parts> skiff::compose::Box<Parts...> card_box(skiff::compose::Look look, Parts... parts) {
  look.fHorizontal = false;
  return skiff::compose::Box<Parts...>(std::move(look), std::move(parts)...);
}
struct card_fit {
  struct State {};
  template <class N> static void prepare(N& card, State&) {
    const skia::SkRect& parent = card.fState.fLastConstraint;
    auto& scroll = std::get<1>(card.fParts);
    const float details = card_part_height(std::get<0>(scroll.fChildren), parent);
    const float others = card.fState.fPadding.totalY() + card_part_height(std::get<0>(card.fParts), parent) +
                         card_rest_height(card.fParts, parent);
    const float room = std::max(0.0f, parent.height() - others);
    scroll.fState.fHeight = parent.height() > 0.0f ? std::min(details, room) : details;
  }
  template <class N> static void arrange(N&, State&) {}
  template <class N> static bool settling(const N&, const State&) { return false; }
  template <class N> static void settle(N&, State&) {}
};

struct person_message_act {
  using Answer = std::tuple<::mux::ui::request::message_person, ::mux::ui::request::close_person_info>;
  conversation_id who;
  Answer operator()() const { return {::mux::ui::request::message_person{who}, ::mux::ui::request::close_person_info{}}; }
};
// What a moderator does to them, as Element's user info offers it.
struct person_room_act {
  using Answer = std::tuple<::mux::ui::request::room_act, ::mux::ui::request::close_person_info>;
  room_action_t action;
  Answer operator()() const { return {::mux::ui::request::room_act{action}, ::mux::ui::request::close_person_info{}}; }
};
// Verified by comparing emoji with each of their devices that answers.
struct person_accept_act {
  using Answer = ::mux::ui::request::accept_identity;
  conversation_id who;
  Answer operator()() const { return ::mux::ui::request::accept_identity{who}; }
};
// A button of its protocol's own: which it is, its request asked again
// where the program knows the protocol.
struct person_protocol_act {
  using Answer = ::mux::ui::request::card_action;
  Answer pressed;
  Answer operator()() const { return pressed; }
};
struct person_verify_act {
  using Answer = std::tuple<::mux::ui::request::verify_person, ::mux::ui::request::close_person_info>;
  conversation_id who;
  Answer operator()() const { return {::mux::ui::request::verify_person{who}, ::mux::ui::request::close_person_info{}}; }
};

// Everything below the title scrolls within the height the dialog gives it.
inline auto person_card_details(const palette& colours, const ui_shared& shared,
                                const account_id& account, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  const scene::Spec tile_spec{.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}};
  const auto& proto_state = protocol_state_of(shared, account);
  // Verify where their protocol verifies people, and they are not, or not
  // any more: not for one verified.
  const bool can_verify = proto::offers(proto_state, proto::feature::identity_verification{}) &&
                          (!facts.trust || !spl::visit(spl::overloaded{[](trust::verified) { return true; },
                                                                       [](const auto&) { return false; }},
                                                       *facts.trust));
  // Their identity reset: taken as theirs now, unverified (Element's
  // "Withdraw verification").
  const bool can_accept = facts.trust && spl::visit(spl::overloaded{[](trust::changed) { return true; },
                                                                    [](const auto&) { return false; }},
                                                    *facts.trust);
  auto sessions = facts.devices | std::views::transform([&](const change::device_view& one) {
                    return c::styled(
                        {.fillX = true, .margin = {2.0f, 22.0f, 0.0f, 22.0f}},
                        elided(nodes::Text(
                            std::format("{} {}{}", one.verified ? "\u2713" : "\u26A0",
                                        one.name.empty() ? one.id : one.name,
                                        one.verified ? std::string(" \u00b7 Verified") : std::string(" \u00b7 Not verified")),
                            13.0f, one.verified ? colours.text : colours.dim)));
                  }) | std::ranges::to<std::vector>();
  auto theirs = proto::person_actions(proto_state, account, key) | std::views::enumerate | std::views::transform([&](const auto& one) {
                  const auto& [index, label] = one;
                  return c::styled(tile_spec, action_tile<person_protocol_act>(
                      colours, label, icon::check{}, {{account, key, static_cast<std::size_t>(index)}}));
                }) | std::ranges::to<std::vector>();
  return c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      profile_cover(colours, key, facts),
      section_band(colours),
      id_line(colours, key, proto::person_link(proto_state, key).value_or("")),
      c::styled(tile_spec, action_tile<person_message_act>(colours, "Message", icon::send{}, {conversation_id{account, key}})),
      c::visible(can_verify, c::styled(tile_spec, action_tile<person_verify_act>(
          colours, "Verify with emoji", icon::check{}, {conversation_id{account, key}}))),
      c::visible(can_accept, c::styled(tile_spec, action_tile<person_accept_act>(
          colours, "Withdraw verification", icon::close{}, {conversation_id{account, key}}))),
      c::visible(facts.may_kick, c::styled(tile_spec, action_tile<person_room_act>(
          colours, "Remove from room", icon::leave{}, {room_action::kick{key}}))),
      c::visible(facts.may_ban, c::styled(tile_spec, action_tile<person_room_act>(
          colours, "Ban from room", icon::close{}, {room_action::ban{key}}))),
      c::styled({.margin = {14.0f, 22.0f, 2.0f, 22.0f}},
          c::visible(!facts.devices.empty(), nodes::Text(
              facts.devices.empty() ? std::string() : std::format("SESSIONS ({})", facts.devices.size()),
              13.0f, colours.dim, true))),
      c::visible(!sessions.empty(), c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(sessions))),
      c::visible(!theirs.empty(), c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(theirs))));
}

inline auto person_card(const palette& colours, const ui_shared& shared,
                        const account_id& account, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  using close_act = sends<::mux::ui::request::close_person_info>;
  auto details = person_card_details(colours, shared, account, key, facts);
  return c::arranged<card_fit>({}, card_box(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}}),
      page_header<no_back, close_act>(colours, "User info", {}, {}, false, true),
      c::styled({.fillX = true}, nodes::ScrollContainer<decltype(details)>(std::move(details)))));
}
inline auto person_card(const ui_needs& n, const person_shown& shown) {
  return person_card(*n.colours, *n.shared, shown.account, shown.key, shown.facts);
}
using person_card_t = decltype(person_card(std::declval<const ui_needs&>(), std::declval<const person_shown&>()));
// tdesktop's profile layer: 392 wide (infoDesiredWidth), as high as what
// it shows, a 24th of the window down within 20 and 40.
inline dialog_look content_look(std::type_identity<person_card_t>) {
  return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}};
}
template <class Needs>
auto make_content(std::type_identity<person_card_t>, const Needs& needs, const person_shown& shown) {
  return person_card(needs, shown);
}

// A room not joined, as a link names it, in the person card's layout: its
// photo beside its name and how many are in it, what it is about, its ID to
// copy, and a button to join it. What its server says (/room_summary) fills
// it when it comes; till then, or where it says nothing, the address alone.
// A room's card open: what was asked for, and what is known of it so far.
struct room_card_facts {
  std::string asked;
  room_preview known;
};
inline auto room_cover(const palette& colours, const std::string& key, const std::string& name, const std::string& line) {
  return skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}}),
      avatar_mark(avatar_of::chat{}, key, name, 72.0f),
      two_lines(colours, name, line, 17.0f, 6.0f, two_line_style{.selectable = true}));
}
using room_cover_t = decltype(room_cover(std::declval<const palette&>(), "", "", ""));

struct join_room_card_act {
  using Answer = std::variant<::mux::ui::request::knock_room_card, ::mux::ui::request::join_room_card>;
  bool knock = false;  // asked to be let in, where it lets people knock
  Answer operator()() const {
    if (knock)
      return ::mux::ui::request::knock_room_card{};
    return ::mux::ui::request::join_room_card{};
  }
};
// Its name, else its address, else what the link said.
inline std::string room_card_name(const std::string& asked, const room_preview& known) {
  return !known.name.empty() ? known.name : !known.alias.empty() ? known.alias : asked;
}
// Under the name: the address, where the name is not it, and how many.
inline std::string room_card_line(const std::string& asked, const room_preview& known) {
  std::string out = known.alias.empty() ? (known.name.empty() ? std::string() : asked) : known.alias;
  if (out == room_card_name(asked, known))
    out.clear();
  if (known.members)
    out += std::format("{}{} {}", out.empty() ? "" : " · ", *known.members, *known.members == 1 ? "member" : "members");
  return out.empty() ? std::string("Room") : out;
}
// The details can be taller than the window. Only this column scrolls;
// closing the card and accepting or declining an invite stay in reach.
inline auto room_card_details(const palette& colours, const std::string& asked, const room_preview& known) {
  namespace c = skiff::compose;
  const std::string& key = known.id.empty() ? asked : known.id;
  nodes::Text about(
      !known.topic.empty()  ? known.topic
      : !known.note.empty() ? known.note
                            : std::string("No description"),
      14.0f, known.topic.empty() ? colours.dim : colours.text);
  about.setSelectable(true);
  return c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      room_cover(colours, key, room_card_name(asked, known), room_card_line(asked, known)),
      section_band(colours),
      c::styled({.fillX = true, .margin = {2.0f, 22.0f, 8.0f, 22.0f}}, wrapped(std::move(about))),
      id_line(colours, key, ""));
}
inline auto room_card(const palette& colours, const std::string& asked, const room_preview& known) {
  namespace c = skiff::compose;
  using close_act = sends<::mux::ui::request::close_room_card>;
  using decline_act = sends<::mux::ui::request::decline_room_card>;
  auto details = room_card_details(colours, asked, known);
  const scene::Spec tile_spec{.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}};
  return c::arranged<card_fit>({}, card_box(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}}),
      page_header<no_back, close_act>(colours, known.invite ? "Invite" : "Room info", {}, {}, false, true),
      c::styled({.fillX = true}, nodes::ScrollContainer<decltype(details)>(std::move(details))),
      c::styled(tile_spec, action_tile<join_room_card_act>(
          colours, known.invite ? "Accept" : known.knock ? "Ask to join" : "Join",
          icon::plus{}, {known.knock && !known.invite})),
      c::visible(known.invite, c::styled(tile_spec,
          action_tile<decline_act>(colours, "Decline", icon::close{}, {})))));
}
inline auto room_card(const ui_needs& n, const room_card_facts& facts) {
  return room_card(*n.colours, facts.asked, facts.known);
}
using room_card_t = decltype(room_card(std::declval<const ui_needs&>(), std::declval<const room_card_facts&>()));
inline dialog_look content_look(std::type_identity<room_card_t>) {
  return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}};
}
template <class Needs>
auto make_content(std::type_identity<room_card_t>, const Needs& needs, const room_card_facts& facts) {
  return room_card(needs, facts);
}

}  // namespace mux::ui

