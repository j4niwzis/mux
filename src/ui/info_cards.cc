// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_cards -- A person's card, and a room's not joined.
export module mux.ui:info_cards;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
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
template <class Actions>
struct person_card : nodes::Stack {
  // tdesktop's profile layer: 392 wide (infoDesiredWidth), as high as what
  // it shows, a 24th of the window down within 20 and 40.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}}; }
  struct message_them {
    Actions* actions = nullptr;
    conversation_id who;
    void operator()() const {
      actions->message_person(who);
      actions->close_person_info();
    }
  };
  using close_act = ask<Actions, &Actions::close_person_info>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header<no_back, close_act>;
  // tdesktop's cover: 108 high, a 72 photo, the name and status beside it.
  struct cover : nodes::Stack {
    struct parts_t {
      avatar_button<Actions> photo;
      two_lines texts;
    } parts;
    cover(Actions* a, const palette& colours, const std::string& key, const person_facts& facts)
        : parts{.photo = avatar_button<Actions>(a, key, facts.name, 72.0f),
                .texts = two_lines(colours, facts.name, facts.status, 17.0f, 6.0f)} {
      parts.texts.parts.name.setSelectable(true);
      parts.texts.parts.state.setSelectable(true);
      this->setHorizontal();
      this->setGap(16.0f);
      fState.apply({.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}});
    }
  };
  // What a moderator does to them, as Element's user info offers it.
  struct to_them {
    Actions* actions = nullptr;
    room_action_t action;
    void operator()() const {
      actions->room_act(action);
      actions->close_person_info();
    }
  };
  // Verified by comparing emoji with each of their devices that answers.
  struct accept_them {
    Actions* actions;
    conversation_id who;
    void operator()() const { actions->accept_identity(who); }
  };
  // A button of its protocol's own: its request asked.
  struct ask_protocol {
    Actions* actions = nullptr;
    proto::any_request_t asks;
    void operator()() const {
      spl::visit(spl::overloaded{[](proto::part::no_request) {}, [&](const auto& one) { actions->ask_for(one); }}, asks);
    }
  };
  struct verify_them {
    Actions* actions = nullptr;
    conversation_id who;
    void operator()() const {
      actions->verify_person(who);
      actions->close_person_info();
    }
  };
  // The colours it is made in.
  const palette* colours_ = nullptr;
  struct parts_t {
    top_bar top;
    cover face;
    nodes::Box<> band;
    id_line id;
    action_tile<message_them> message;
    action_tile<verify_them> verify;
    // Their identity reset: taken as theirs now, unverified (Element's
    // "Withdraw verification").
    action_tile<accept_them> accept;
    action_tile<to_them> remove;
    action_tile<to_them> ban;
    // Their sessions, as Element lists them on a person: each with its
    // name or id, and verified or not.
    nodes::Text sessions_title;
    std::vector<nodes::Text> sessions;
    // Its protocol's own buttons for them (proto::person_actions).
    std::vector<action_tile<ask_protocol>> theirs;
  } parts;

  person_card(Actions* a, const palette& colours, const ui_shared& shared, const account_id& account, const std::string& key, const person_facts& facts)
      : colours_(&colours),
        parts{.top = top_bar(colours, "User info", {}, {a}, false, true),
              .face = cover(a, colours, key, facts),
              .band = section_band(colours),
              .id = id_line(colours, key, ""),
              .message = action_tile<message_them>(colours, "Message", icon::send{}, {a, conversation_id{account, key}}),
              .verify = action_tile<verify_them>(colours, "Verify with emoji", icon::check{}, {a, conversation_id{account, key}}),
              .accept = action_tile<accept_them>(colours, "Withdraw verification", icon::close{}, {a, conversation_id{account, key}}),
              .remove = action_tile<to_them>(colours, "Remove from room", icon::leave{}, {a, room_action::kick{key}}),
              .ban = action_tile<to_them>(colours, "Ban from room", icon::close{}, {a, room_action::ban{key}}),
              .sessions_title = nodes::Text("", 13.0f, colours.dim, true)} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.message, &parts.verify, &parts.accept, &parts.remove, &parts.ban})
      each->apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    // Offered only where the user may: no button for what they cannot do.
    parts.remove.setVisible(facts.may_kick);
    parts.ban.setVisible(facts.may_ban);
    std::ranges::for_each(proto::person_actions(protocol_state_of(shared, account), account, key), [&](proto::any_action& one) {
      if (one.asks)
        parts.theirs.emplace_back(colours, one.label, icon::check{}, ask_protocol{a, std::move(*one.asks)})
            .apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    });
    parts.sessions_title.setText(facts.devices.empty() ? std::string()
                                                       : std::format("SESSIONS ({})", facts.devices.size()));
    parts.sessions_title.setVisible(!facts.devices.empty());
    parts.sessions_title.apply({.margin = {14.0f, 22.0f, 2.0f, 22.0f}});
    for (const change::device_view& one : facts.devices) {
      auto& line = parts.sessions.emplace_back(
          std::format("{} {}{}", one.verified ? "\u2713" : "\u26A0", one.name.empty() ? one.id : one.name,
                      one.verified ? std::string(" \u00b7 Verified") : std::string(" \u00b7 Not verified")),
          13.0f, one.verified ? colours.text : colours.dim);
      line.setElided(true);
      line.apply({.fillX = true, .margin = {2.0f, 22.0f, 0.0f, 22.0f}});
    }
    parts.accept.setVisible(facts.trust && spl::visit(spl::overloaded{[](trust::changed) { return true; },
                                                                            [](const auto&) { return false; }},
                                                         *facts.trust));
    // Verify where their protocol verifies people, and they are not, or not
    // any more: not for one verified.
    parts.verify.setVisible(proto::offers(protocol_state_of(shared, account), proto::feature::identity_verification{}) &&
                            (!facts.trust || !spl::visit(spl::overloaded{[](trust::verified) { return true; },
                                                                               [](const auto&) { return false; }},
                                                            *facts.trust)));
  }
};

// A room not joined, as a link names it, in the person card's layout: its
// photo beside its name and how many are in it, what it is about, its ID to
// copy, and a button to join it. What its server says (/room_summary) fills
// it when it comes; till then, or where it says nothing, the address alone.
template <class Actions>
struct room_card : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{392.0f}, .place = widgets::dialog_place::near_top{}}; }
  struct join_it {
    Actions* actions = nullptr;
    bool knock = false;  // asked to be let in, where it lets people knock
    void operator()() const {
      if (knock)
        actions->knock_room_card();
      else
        actions->join_room_card();
    }
  };
  struct decline_it {
    Actions* actions = nullptr;
    void operator()() const { actions->decline_room_card(); }
  };
  using close_act = ask<Actions, &Actions::close_room_card>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header<no_back, close_act>;
  struct cover : nodes::Stack {
    struct parts_t {
      avatar_mark photo;
      two_lines texts;
    } parts;
    cover(const palette& colours, const std::string& key, const std::string& name, const std::string& line)
        : parts{.photo = avatar_mark(key, name, 72.0f), .texts = two_lines(colours, name, line, 17.0f, 6.0f)} {
      this->setHorizontal();
      this->setGap(16.0f);
      fState.apply({.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}});
    }
  };
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
  struct details : nodes::Stack {
    struct parts_t {
      cover face;
      nodes::Box<> band;
      nodes::Text about;
      id_line id;
    } parts;

    details(const palette& colours, const std::string& asked, const room_preview& known)
        : parts{.face = cover(colours, known.id.empty() ? asked : known.id, name_of(asked, known), line_of(asked, known)),
                .band = section_band(colours),
                .about = nodes::Text(!known.topic.empty() ? known.topic : !known.note.empty() ? known.note : std::string("No description"), 14.0f,
                                     known.topic.empty() ? colours.dim : colours.text),
                .id = id_line(colours, known.id.empty() ? asked : known.id, "")} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.about.setWrapped(true);
      parts.about.apply({.fillX = true, .margin = {2.0f, 22.0f, 8.0f, 22.0f}});
      parts.about.setSelectable(true);
      parts.face.parts.texts.parts.name.setSelectable(true);
      parts.face.parts.texts.parts.state.setSelectable(true);
    }
  };
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<details> scroll;
    action_tile<join_it> join;
    // An invite's: let go of.
    std::optional<action_tile<decline_it>> decline;
  } parts;

  room_card(Actions* a, const palette& colours, const std::string& asked, const room_preview& known)
      : parts{.top = top_bar(colours, "Room info", {}, {a}, false, true),
              .scroll = nodes::ScrollContainer<details>(details(colours, asked, known)),
              .join = action_tile<join_it>(colours, known.invite ? "Accept" : known.knock ? "Ask to join" : "Join", icon::plus{},
                                           {a, known.knock && !known.invite})} {
    fState.apply({.fillX = true, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    parts.scroll.apply({.fillX = true, .grow = scene::axes::kY});
    if (known.invite) {
      parts.top.parts.title.setText("Invite");
      parts.decline.emplace(colours, "Decline", icon::close{}, decline_it{a});
      parts.decline->apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    }
    parts.join.apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
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
