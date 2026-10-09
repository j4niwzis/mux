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
template <class Actions>
auto person_cover(const palette& colours, const std::string& key, const person_facts& facts) {
  return skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}}),
      avatar_button<Actions>(key, facts.name, 72.0f),
      two_lines(colours, facts.name, facts.status, 17.0f, 6.0f, two_line_style{.selectable = true}));
}
template <class Actions>
using person_cover_t = decltype(person_cover<Actions>(std::declval<const palette&>(), "", std::declval<const person_facts&>()));

template <class Actions> struct person_card : skiff::compose::Stacked {
  // tdesktop's profile layer: 392 wide (infoDesiredWidth), as high as what
  // it shows, a 24th of the window down within 20 and 40.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}}; }
  struct message_them {
    using Answer = std::tuple<::mux::ui::request::message_person, ::mux::ui::request::close_person_info>;
    conversation_id who;
    Answer operator()() const { return {::mux::ui::request::message_person{who}, ::mux::ui::request::close_person_info{}}; }
  };
  using close_act = sends<::mux::ui::request::close_person_info>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header_t<no_back, close_act>;
  // tdesktop's cover: 108 high, a 72 photo, the name and status beside it.
  // What a moderator does to them, as Element's user info offers it.
  struct to_them {
    using Answer = std::tuple<::mux::ui::request::room_act, ::mux::ui::request::close_person_info>;
    room_action_t action;
    Answer operator()() const { return {::mux::ui::request::room_act{action}, ::mux::ui::request::close_person_info{}}; }
  };
  // Verified by comparing emoji with each of their devices that answers.
  struct accept_them {
    using Answer = ::mux::ui::request::accept_identity;
    conversation_id who;
    ::mux::ui::request::accept_identity operator()() { return ::mux::ui::request::accept_identity{who}; }
  };
  // A button of its protocol's own: which it is, its request asked again
  // where the program knows the protocol.
  struct ask_protocol {
    using Answer = ::mux::ui::request::card_action;
    Answer pressed;
    Answer operator()() const { return pressed; }
  };
  struct verify_them {
    using Answer = std::tuple<::mux::ui::request::verify_person, ::mux::ui::request::close_person_info>;
    conversation_id who;
    Answer operator()() const { return {::mux::ui::request::verify_person{who}, ::mux::ui::request::close_person_info{}}; }
  };
  // The colours it is made in.
  const palette* colours_ = nullptr;
  struct parts_t {
    top_bar top;
    person_cover_t<Actions> face;
    nodes::Box<> band;
    id_line_t id;
    action_tile_t<message_them> message;
    action_tile_t<verify_them> verify;
    // Their identity reset: taken as theirs now, unverified (Element's
    // "Withdraw verification").
    action_tile_t<accept_them> accept;
    action_tile_t<to_them> remove;
    action_tile_t<to_them> ban;
    // Their sessions, as Element lists them on a person: each with its
    // name or id, and verified or not.
    nodes::Text sessions_title;
    std::vector<nodes::Text> sessions;
    // Its protocol's own buttons for them (proto::person_actions).
    std::vector<action_tile_t<ask_protocol>> theirs;
  } parts;

  person_card(const ui_needs<Actions>& n, const person_shown& shown)
      : person_card(*n.colours, *n.shared, shown.account, shown.key, shown.facts) {}
  person_card(const palette &colours, const ui_shared &shared,
              const account_id &account, const std::string &key,
              const person_facts &facts)
      : Stacked(
            skiff::compose::vbox(0.0f, {.fillX = true,
                                        .autoSize = scene::axes::kY,
                                        .padding = {0.0f, 0.0f, 16.0f, 0.0f}})),
        colours_(&colours),
        parts{.top = page_header<no_back, close_act>(colours, "User info", {}, {}, false, true),
              .face = person_cover<Actions>(colours, key, facts),
              .band = section_band(colours),
              .id = id_line(colours, key, ""),
              .message =
                  action_tile<message_them>(colours, "Message", icon::send{},
                                            {conversation_id{account, key}}),
              .verify = skiff::compose::visible(
                  proto::offers(protocol_state_of(shared, account),
                                proto::feature::identity_verification{}) &&
                      (!facts.trust ||
                       !spl::visit(
                           spl::overloaded{[](trust::verified) { return true; },
                                           [](const auto &) { return false; }},
                           *facts.trust)),
                  action_tile<verify_them>(colours, "Verify with emoji",
                                           icon::check{},
                                           {conversation_id{account, key}})),
              .accept = skiff::compose::visible(
                  facts.trust &&
                      spl::visit(
                          spl::overloaded{[](trust::changed) { return true; },
                                          [](const auto &) { return false; }},
                          *facts.trust),
                  action_tile<accept_them>(colours, "Withdraw verification",
                                           icon::close{},
                                           {conversation_id{account, key}})),
              .remove = skiff::compose::visible(
                  facts.may_kick, action_tile<to_them>(
                                      colours, "Remove from room",
                                      icon::leave{}, {room_action::kick{key}})),
              .ban = skiff::compose::visible(
                  facts.may_ban,
                  action_tile<to_them>(colours, "Ban from room", icon::close{},
                                       {room_action::ban{key}})),
              .sessions_title = skiff::compose::styled(
                  {.margin = {14.0f, 22.0f, 2.0f, 22.0f}},
                  skiff::compose::visible(
                      !facts.devices.empty(),
                      nodes::Text("", 13.0f, colours.dim, true)))} {
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.message, &parts.verify, &parts.accept, &parts.remove, &parts.ban})
      each->apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    // Offered only where the user may: no button for what they cannot do.

    std::ranges::for_each(std::views::enumerate(proto::person_actions(protocol_state_of(shared, account), account, key)), [&](const auto& one) {
      const auto& [index, label] = one;
      parts.theirs.push_back(skiff::compose::styled({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}},
          action_tile<ask_protocol>(colours, label, icon::check{}, {{account, key, static_cast<std::size_t>(index)}})));
    });
    parts.sessions_title.setText(facts.devices.empty() ? std::string()
                                                       : std::format("SESSIONS ({})", facts.devices.size()));

    for (const change::device_view& one : facts.devices) {
      auto& line = parts.sessions.emplace_back(
          std::format("{} {}{}", one.verified ? "\u2713" : "\u26A0", one.name.empty() ? one.id : one.name,
                      one.verified ? std::string(" \u00b7 Verified") : std::string(" \u00b7 Not verified")),
          13.0f, one.verified ? colours.text : colours.dim);
      line.setElided(true);
      line.apply({.fillX = true, .margin = {2.0f, 22.0f, 0.0f, 22.0f}});
    }
    // Verify where their protocol verifies people, and they are not, or not
    // any more: not for one verified.
  }
};

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
      avatar_mark(key, name, 72.0f),
      two_lines(colours, name, line, 17.0f, 6.0f, two_line_style{.selectable = true}));
}
using room_cover_t = decltype(room_cover(std::declval<const palette&>(), "", "", ""));

template <class Actions> struct room_card : skiff::compose::Stacked {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}}; }
  struct join_it {
    using Answer = std::variant<::mux::ui::request::knock_room_card, ::mux::ui::request::join_room_card>;
    bool knock = false;  // asked to be let in, where it lets people knock
    Answer operator()() const {
      if (knock)
        return ::mux::ui::request::knock_room_card{};
      return ::mux::ui::request::join_room_card{};
    }
  };
  struct decline_it {
    using Answer = ::mux::ui::request::decline_room_card;
    ::mux::ui::request::decline_room_card operator()() { return ::mux::ui::request::decline_room_card{}; }
  };
  using close_act = sends<::mux::ui::request::close_room_card>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header_t<no_back, close_act>;
  // Its name, else its address, else what the link said.
  static std::string name_of(const std::string& asked, const room_preview& known) {
    return !known.name.empty() ? known.name : !known.alias.empty() ? known.alias : asked;
  }
  // Under the name: the address, where the name is not it, and how many.
  static std::string line_of(const std::string& asked, const room_preview& known) {
    std::string out = known.alias.empty() ? (known.name.empty() ? std::string() : asked) : known.alias;
    if (out == name_of(asked, known))
      out.clear();
    if (known.members)
      out += std::format("{}{} {}", out.empty() ? "" : " · ", *known.members, *known.members == 1 ? "member" : "members");
    return out.empty() ? std::string("Room") : out;
  }
  // The details can be taller than the window. Only this column scrolls;
  // closing the card and accepting or declining an invite stay in reach.
  struct details : skiff::compose::Stacked {
    struct parts_t {
      room_cover_t face;
      nodes::Box<> band;
      nodes::Text about;
      id_line_t id;
    } parts;

    details(const palette &colours, const std::string &asked,
            const room_preview &known)
        : Stacked(skiff::compose::vbox(
              0.0f, {.fillX = true, .autoSize = scene::axes::kY})),
          parts{.face = room_cover(colours, known.id.empty() ? asked : known.id,
                              name_of(asked, known), line_of(asked, known)),
                .band = section_band(colours),
                .about = skiff::compose::styled(
                    {.fillX = true, .margin = {2.0f, 22.0f, 8.0f, 22.0f}},
                    wrapped(nodes::Text(
                        !known.topic.empty()  ? known.topic
                        : !known.note.empty() ? known.note
                                              : std::string("No description"),
                        14.0f,
                        known.topic.empty() ? colours.dim : colours.text))),
                .id =
                    id_line(colours, known.id.empty() ? asked : known.id, "")} {

      parts.about.setSelectable(true);
    }
  };
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<details> scroll;
    action_tile_t<join_it> join;
    // An invite's: let go of.
    std::optional<action_tile_t<decline_it>> decline;
  } parts;

  room_card(const ui_needs<Actions>& n, const room_card_facts& facts) : room_card(*n.colours, facts.asked, facts.known) {}
  room_card(const palette &colours, const std::string &asked,
            const room_preview &known)
      : Stacked(skiff::compose::vbox(
            0.0f, {.fillX = true, .padding = {0.0f, 0.0f, 16.0f, 0.0f}})),
        parts{.top = page_header<no_back, close_act>(colours, "Room info", {}, {}, false, true),
              .scroll = skiff::compose::styled(
                  {.fillX = true, .grow = scene::axes::kY},
                  nodes::ScrollContainer<details>(
                      details(colours, asked, known))),
              .join = skiff::compose::styled(
                  {.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}},
                  action_tile<join_it>(colours,
                                       known.invite  ? "Accept"
                                       : known.knock ? "Ask to join"
                                                     : "Join",
                                       icon::plus{},
                                       {known.knock && !known.invite}))} {

    if (known.invite) {
      std::get<1>(parts.top.fParts).setText("Invite");
      parts.decline.emplace(action_tile<decline_it>(colours, "Decline", icon::close{}, {}));
      parts.decline->apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    }
  }

  void measure(const skia::SkRect& parent) {
    // Keep a short card compact. For a long one, size the viewport to the
    // room the dialog gives us, not to the description's unbounded height.
    const auto height_of = [&](auto& node) {
      // Measure in its previous position so the scroll container's anchor
      // is not moved before it has had a chance to remember that position.
      const auto previous = node.fState.fLastConstraint;
      const auto room = skia::SkRect::MakeXYWH(previous.fLeft, previous.fTop, parent.width(), 0.0f);
      scene::layout(node, room);
      return node.bounds().height() + node.fState.fMargin.totalY();
    };
    const float natural = height_of(parts.top) + height_of(std::get<0>(parts.scroll.fChildren)) +
                          height_of(parts.join) + (parts.decline ? height_of(*parts.decline) : 0.0f) +
                          fState.fPadding.totalY();
    fState.fHeight = std::min(natural, std::max(0.0f, parent.height()));
  }
};

}  // namespace mux::ui
