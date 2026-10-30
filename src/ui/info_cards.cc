// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_cards -- A person's card, and a room's not joined.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
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
import mux.ui.lists;
import mux.ui.shaping;
import mux.ui.messages;
import mux.ui.cards;

export namespace mux::ui {
// A person's info, as tdesktop's profile layer: a box in the middle of the
// window over the chats -- a bar with its title and ✕, their photo beside
// their name and how they are, their ID to copy, and a message to them.
// A person's card open: from which account, who, and what is known of them.
struct person_shown {
  account_id account;
  std::string key;
  person_facts facts;
  friend bool operator==(const person_shown&, const person_shown&) = default;
};
// Compared: shown again as it is -- their devices listed, their trust told,
// their profile found, unchanged -- the card is not made anew (show), and
// keeps where it was scrolled.
static_assert(std::equality_comparable<person_shown>);
auto person_cover(const palette& colours, const std::string& key, const person_facts& facts) {
  return SKIFF_NAMED(person_cover, skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}}),
      // Their name and how they are, a piece (the avatars plugin's picture
      // beside it): the card a builder, built as its dialog opens.
      person_cover_text(colours, key, facts.name, facts.status)));
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
MUX_INLINE_IN_RELEASE auto profile_cover_details(const palette& colours, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(profile_cover_details, c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      person_cover(colours, key, facts),
      c::visible(!facts.profile.pronouns.empty(), c::styled(
          {.fillX = true, .margin = {0.0f, 22.0f, 8.0f, 22.0f}},
          wrapped(nodes::Text(profile_pronouns(facts.profile), 13.0f, colours.dim)))),
      c::visible(!facts.profile.biography.plain.empty() || facts.profile.biography.html.has_value(),
          profile_biography(colours, facts.profile.biography))));
}
// The banner sits behind the name, pronouns and biography. Its background
// ends at the section divider, leaving the account ID and actions plain.
MUX_INLINE_IN_RELEASE auto profile_cover(const palette& colours, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  const bool has_banner = facts.profile.banner.has_value();
  nodes::Image<from_moving_thumbnail> banner(from_moving_thumbnail{facts.profile.banner.value_or("")});
  banner.keepBox();
  return SKIFF_NAMED(profile_cover, c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::visible(has_banner, c::styled({.place = scene::anchor::kTopLeft, .fill = true}, std::move(banner))),
      c::visible(has_banner, c::styled({.place = scene::anchor::kTopLeft, .fill = true}, nodes::Box<>(skia::colorSetARGB(96, 0, 0, 0)))),
      profile_cover_details(profile_text_colours(colours, has_banner), key, facts)));
}
using profile_cover_t = decltype(profile_cover(std::declval<const palette&>(), "", std::declval<const person_facts&>()));

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

// Verify, where their protocol verifies people -- known from its type, the
// tile not made for one that does not -- shown while their identity is
// known (told by end-to-end encryption, the encryption plugin's) and they
// are not verified, or not any more.
template <class Protocol>
auto verify_part(const palette&, const scene::Spec&, const account_id&, const std::string&, bool) {
  return nothing_shown();
}
template <class Protocol>
  requires(proto::offered_by<state_of<Protocol>, proto::feature::identity_verification>())
auto verify_part(const palette& colours, const scene::Spec& tile_spec, const account_id& account, const std::string& key, bool unverified) {
  namespace c = skiff::compose;
  return c::visible(unverified, c::styled(tile_spec, action_tile<person_verify_act>(
      colours, "Verify with emoji", icon::check{}, {conversation_id{account, key}})));
}
// Everything below the title scrolls within the height the dialog gives it.
// Of a protocol's account's person.
template <class Protocol>
auto person_card_details(const palette& colours, const ui_shared& shared,
                         const account_id& account, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  const scene::Spec tile_spec{.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}};
  const auto& proto_state = protocol_state_of(shared, account);
  const bool unverified = facts.trust && !spl::visit(spl::overloaded{[](trust::verified) { return true; },
                                                                     [](const auto&) { return false; }},
                                                     *facts.trust);
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
  return SKIFF_NAMED(person_card_details, c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      profile_cover(colours, key, facts),
      section_band(colours),
      id_line(colours, key, proto::person_link(proto_state, key).value_or("")),
      c::styled(tile_spec, action_tile<person_message_act>(colours, "Message", icon::send{}, {conversation_id{account, key}})),
      verify_part<Protocol>(colours, tile_spec, account, key, unverified),
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
      c::visible(!theirs.empty(), c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(theirs)))));
}

// What the card holds of its protocol's: its details, one type for each
// protocol -- the account's protocol read once, here -- in a column, as the
// side's accounts and pages are (a dialog's own content stays one type).
template <class>
struct person_details_of;
template <class... Protocols>
struct person_details_of<protocol_list<Protocols...>> {
  using type = spl::variant<decltype(person_card_details<Protocols>(std::declval<const palette&>(), std::declval<const ui_shared&>(),
                                                                    std::declval<const account_id&>(), std::string(),
                                                                    std::declval<const person_facts&>()))...>;
};
using person_details_t = typename person_details_of<protocols>::type;
MUX_INLINE_IN_RELEASE auto person_card(const palette& colours, const ui_shared& shared,
                        const account_id& account, const std::string& key, const person_facts& facts) {
  namespace c = skiff::compose;
  using close_act = sends<::mux::ui::request::close_person_info>;
  auto details = c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spl::visit([&]<class Protocol>(Protocol) {
        return person_details_t(person_card_details<Protocol>(colours, shared, account, key, facts));
      }, account.speaks));
  return c::arranged<card_fit>({}, card_box(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}}),
      page_header<no_back, close_act>(colours, "User info", {}, {}, false, true),
      c::styled({.fillX = true}, c::scrolled(std::move(details)))));
}
MUX_INLINE_IN_RELEASE auto person_card(const ui_needs& n, const person_shown& shown) {
  return person_card(*n.colours, *n.shared, shown.account, shown.key, shown.facts);
}
using person_card_t = decltype(person_card(std::declval<const ui_needs&>(), std::declval<const person_shown&>()));
// tdesktop's profile layer: 392 wide (infoDesiredWidth), as high as what
// it shows, a 24th of the window down within 20 and 40.
inline dialog_look content_look(std::type_identity<person_shown>) {
  return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}};
}
template <class Needs>
auto make_content(const Needs& needs, const person_shown& shown) {
  return person_card(needs, shown);
}

}  // namespace mux::ui

