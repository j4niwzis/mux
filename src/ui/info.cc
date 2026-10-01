// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info -- A chat's info, and a person's.
export module mux.ui:info;

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

export namespace mux::ui {

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
struct action_tile : nodes::Stack {
  Act act;
  struct parts_t {
    icon_mark mark;
    nodes::Text label;
  } parts;

  // Declared: the icon at the top, the name at the bottom.
  action_tile(std::string text, icon_t icon, Act what = {})
      : act(std::move(what)), parts{.mark = icon_mark(icon), .label = nodes::Text(std::move(text), 12.0f, text_colour)} {
    auto& [mark, label] = parts;
    fState.apply({.height = 58.0f, .padding = {6.0f, 0.0f, 8.0f, 0.0f}, .cornerRadius = 8.0f, .background = tile_colour, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
    fStack.justify = nodes::justify::space_between{};
    mark.setColour(text_colour);
    mark.apply({.height = 24.0f});
    label.apply({.alignSelf = scene::align::kMiddle});
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = parts.label.text();
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// Someone in a group, in its info: avatar, name, how they are, and their
// role in a pill. Pressed, they are shown on a page of their own.
template <class Open>
struct member_row : nodes::Stack {
  // Who it shows and how they are: while the same, the row is kept.
  member who;
  std::string how_shown;
  Open open;
  std::string id;
  std::optional<std::string> role;
  // Their name, and how they are under it.
  struct texts_column : two_lines {
    texts_column(std::string shown, std::string how) : two_lines(std::move(shown), std::move(how), 14.0f, 4.0f) {}
  };
  // Their role, in a pill beside their name.
  struct role_pill : widgets::Pill {
    explicit role_pill(std::string what)
        : widgets::Pill(std::move(what), {.plate = skia::colorSetARGB(255, 62, 52, 96),
                                          .text = skia::colorSetARGB(255, 190, 170, 250),
                                          .size = 12.0f,
                                          .height = 20.0f,
                                          .padX = 8.0f}) {
      fState.apply({.alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    }
  };
  struct parts_t {
    avatar_mark face;
    texts_column texts;
    role_pill pill;
  } parts;

  // Declared: the avatar, the name over how they are, the role at the end.
  member_row(const member& one, std::string how, Open what)
      : who(one), how_shown(how), open(std::move(what)), id(one.id), role(one.role),
        parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 40.0f),
              .texts = texts_column(one.name.empty() ? one.id : one.name, std::move(how)),
              .pill = role_pill(one.role.value_or(""))} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = chosen_colour});
    fState.setRecorded(true);  // played back as the list repaints around it
    parts.pill.setVisible(one.role.has_value());
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    open(*this);
    return true;
  }
};

// A round avatar on its own: the chat's, big, over its name.
struct big_avatar : avatar_mark {
  big_avatar() : avatar_mark(std::string(), std::string(), 96.0f) {}
};
// An icon on its own, not to be pressed.
struct icon_view : nodes::Icon {
  explicit icon_view(icon_t mark) : nodes::Icon(shape_of(mark), dim_colour) { fState.apply({.width = 28.0f, .height = 36.0f}); }
};
// A band between sections: just darker than the panel.
inline nodes::Box<> section_band() {
  nodes::Box<> out{section_colour};
  out.apply({.fillX = true, .height = 6.0f, .margin = {6.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
// Declared: a column of these, nothing placed by hand.
// An ID, whole -- wrapped, never cut -- and copied when pressed: a chat's
// or a person's.
struct id_line : nodes::Stack {
  struct parts_t {
    nodes::Text id;
    nodes::Text label{"ID", 12.0f, dim_colour};
  } parts;
  std::string copied;
  bool a_link = false;  // what is copied is a link to it, not the ID
  std::string named;    // what it is: ID, Address
  id_line(std::string text, std::string link, std::string label = "ID")
      : parts{.id = nodes::Text(text, 14.0f, accent_colour), .label = nodes::Text(label, 12.0f, dim_colour)},
        copied(link.empty() ? text : link), a_link(!link.empty()), named(std::move(label)) {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
    fState.setCursor(scene::cursor::hand{});
    parts.id.setWrapped(true);
    parts.id.apply({.fillX = true});
    // Selectable, as any text shown: a drag takes part of it, the right
    // button its Copy; a click still copies it whole.
    parts.id.setSelectable(true);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    skiff::scene::setClipboardText(copied);
    parts.label.setText(a_link ? named + " · link copied, with its servers" : named + " · copied");
    return true;
  }
};

// A person's name and how they are, as a chat knows them: a member's, with
// their role; the other side of a direct chat; anyone else by their address.
struct person_facts {
  std::string name;
  std::string status;
  // What the user may do to them in the chat being read, as its power
  // levels allow: only above them, and only where the room lets them.
  bool may_kick = false;
  bool may_ban = false;
};
[[nodiscard]] inline person_facts person_of(const conversation* in, const model& now, const account_id& account,
                                            const std::string& id) {
  person_facts out{id, presence_of(now, account, id)};
  if (in == nullptr)
    return out;
  if (const auto found = std::ranges::find(in->members, id, &member::id); found != in->members.end()) {
    const auto level_of = [&](const std::string& who) {
      const auto level = in->powers.find(who);
      return level == in->powers.end() ? in->power_default : level->second;
    };
    const std::int64_t mine = level_of(account.address), theirs = level_of(id);
    out.may_kick = id != account.address && mine > theirs && mine >= in->needs.of(power_need::kick{});
    out.may_ban = id != account.address && mine > theirs && mine >= in->needs.of(power_need::ban{});
    if (!found->name.empty())
      out.name = found->name;
    if (found->role)
      out.status = out.status.empty() ? *found->role : std::format("{} · {}", out.status, *found->role);
  } else if (!is_group(*in) && id == contact_of(*in)) {
    out.name = display_name(*in);
  }
  return out;
}

// A person's info, as tdesktop's profile layer: a box in the middle of the
// window over the chats -- a bar with its title and ✕, their photo beside
// their name and how they are, their ID to copy, and a message to them.
template <class Actions>
struct person_card : nodes::Stack {
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
    cover(Actions* a, const std::string& key, const person_facts& facts)
        : parts{.photo = avatar_button<Actions>(a, key, facts.name, 72.0f),
                .texts = two_lines(facts.name, facts.status, 17.0f, 6.0f)} {
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
  struct parts_t {
    top_bar top;
    cover face;
    nodes::Box<> band = section_band();
    id_line id;
    action_tile<message_them> message;
    action_tile<to_them> remove;
    action_tile<to_them> ban;
  } parts;

  person_card(Actions* a, const account_id& account, const std::string& key, const person_facts& facts)
      : parts{.top = top_bar("User info", {}, {a}, false, true),
              .face = cover(a, key, facts),
              .id = id_line(key, ""),
              .message = action_tile<message_them>("Message", icon::send{}, {a, conversation_id{account, key}}),
              .remove = action_tile<to_them>("Remove from room", icon::leave{}, {a, room_action::kick{key}}),
              .ban = action_tile<to_them>("Ban from room", icon::close{}, {a, room_action::ban{key}})} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.message, &parts.remove, &parts.ban})
      each->apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    // Offered only where the user may: no button for what they cannot do.
    parts.remove.setVisible(facts.may_kick);
    parts.ban.setVisible(facts.may_ban);
  }
};

// A room not joined, as a link names it, in the person card's layout: its
// photo beside its name and how many are in it, what it is about, its ID to
// copy, and a button to join it. What its server says (/room_summary) fills
// it when it comes; till then, or where it says nothing, the address alone.
template <class Actions>
struct room_card : nodes::Stack {
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
    cover(const std::string& key, const std::string& name, const std::string& line)
        : parts{.photo = avatar_mark(key, name, 72.0f), .texts = two_lines(name, line, 17.0f, 6.0f)} {
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
    return out.empty() ? std::string("Matrix room") : out;
  }
  struct parts_t {
    top_bar top;
    cover face;
    nodes::Box<> band = section_band();
    nodes::Text about;
    id_line id;
    action_tile<join_it> join;
    // An invite's: let go of.
    std::optional<action_tile<decline_it>> decline;
  } parts;

  room_card(Actions* a, const std::string& asked, const room_preview& known)
      : parts{.top = top_bar("Room info", {}, {a}, false, true),
              .face = cover(known.id.empty() ? asked : known.id, name_of(asked, known), line_of(asked, known)),
              .about = nodes::Text(!known.topic.empty() ? known.topic : !known.note.empty() ? known.note : std::string("No description"), 14.0f,
                                   known.topic.empty() ? dim_colour : text_colour),
              .id = id_line(known.id.empty() ? asked : known.id, ""),
              .join = action_tile<join_it>(known.invite ? "Accept" : known.knock ? "Ask to join" : "Join", icon::plus{},
                                           {a, known.knock && !known.invite})} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    if (known.invite) {
      parts.top.parts.title.setText("Invite");
      parts.decline.emplace("Decline", icon::close{}, decline_it{a});
      parts.decline->apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
    }
    parts.about.setWrapped(true);
    parts.about.apply({.fillX = true, .margin = {2.0f, 22.0f, 8.0f, 22.0f}});
    parts.about.setSelectable(true);
    parts.face.parts.texts.parts.name.setSelectable(true);
    parts.face.parts.texts.parts.state.setSelectable(true);
    parts.join.apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
  }
};

// A reaction as the event it is: who, with what, when.
struct reaction_entry {
  std::string event;
  std::string who;
  std::string name;
  std::string key;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  bool mine = false;
  std::string to;  // the message reacted to
};

// A message's reactions as events, as Matrix has them: a box in the middle,
// a row for each -- the person's avatar and name over what they reacted
// with and when. A press on one answers it: the reaction is an event, and a
// message can reply to it.
template <class Actions>
struct reactions_box : nodes::Stack {
  using close_act = ask<Actions, &Actions::close_reactions>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header<no_back, close_act>;
  // A reaction as the chat would show it: a bubble from who reacted,
  // saying what they reacted with, in runs as the chat's bubbles are.
  // Pressed, it is answered.
  struct row : nodes::Stack {
    Actions* actions;
    reaction_entry entry;
    struct parts_t {
      message_bubble bubble;
    } parts;
    row(Actions* a, const conversation& in, reaction_entry one, bool first, bool last, const model* now)
        : actions(a), entry(one), parts{.bubble = message_bubble(in, message_of(in, one), first, last, now)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                    .hoverBackground = chosen_colour});
      fState.setCursor(scene::cursor::hand{});
    }
    // What it said, as a message: its key; a picture's, as a custom emoji
    // in HTML, as a message would carry it.
    [[nodiscard]] static message message_of(const conversation& in, const reaction_entry& one) {
      message out{.in = in.id, .id = one.event, .sender = one.who, .at = one.at, .body = {one.key, std::nullopt},
                  .outgoing = one.mine};
      if (one.key.starts_with("mxc://"))
        out.body = {":emoji:", std::format("<img data-mx-emoticon src=\"{}\" alt=\":emoji:\" height=\"32\">", one.key)};
      return out;
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    // Its menu, as a message's in the chat: Copy -- what is selected, the
    // link under the pointer, the preview's -- and, one's own, the reaction
    // changed to another from the menu's reactions, or taken back.
    using scene::Node::onPointer;
    void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
      if (press.button != 3)
        return;
      const message_bubble& one = parts.bubble;
      menu_facts facts;
      facts.id = entry.event;
      facts.own = entry.mine;
      facts.text = one.plain;
      facts.selection = one.parts.body.parts.text.hasSelection();
      facts.copied = facts.selection ? one.parts.body.parts.text.selected() : one.plain;
      facts.deletable = entry.mine && !entry.event.empty();
      if (const auto& asked = skiff::nodes::textMenusAsked(); !asked.empty() && asked.back().link)
        facts.pressed_link = *asked.back().link;
      else if (const auto& preview = one.parts.body.parts.preview; preview && preview->fState.fBounds.contains(press.x, press.y))
        facts.pressed_link = preview->url;
      if (!entry.event.empty() && !entry.to.empty())
        facts.reaction = menu_facts::reaction_facts{entry.to, entry.key};
      facts.x = press.x;
      facts.y = press.y;
      actions->message_menu(std::move(facts));
      reply.handle();
    }
    [[nodiscard]] bool onClick(float x, float y) {
      // A link's preview or card in it: followed, as in the chat.
      const message_bubble& one = parts.bubble;
      if (const auto& preview = one.parts.body.parts.preview; preview && preview->bounds().contains(x, y)) {
        actions->open_url(preview->url);
        return true;
      }
      for (const link_card& card : one.parts.body.parts.cards)
        if (card.bounds().contains(x, y)) {
          actions->open_url(card.url);
          return true;
        }
      // Answered, where it is an event of its own to answer.
      if (!entry.event.empty())
        actions->reply_to(entry.event, std::format("{} reacted {}", entry.name, entry.key));
      actions->close_reactions();
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  reactions_box(Actions* a, const conversation& in, const std::vector<reaction_entry>& entries, const model* now)
      : parts{.top = top_bar("Reactions", {}, {a}, false, true)} {
    fState.apply({.fillX = true, .height = 420.0f, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    auto& flow = std::get<0>(parts.list.fChildren);
    flow.apply({.fillX = true, .autoSize = scene::axes::kY});
    auto& rows = std::get<0>(flow.fChildren);
    rows.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
      rows.emplace_back(a, in, entries[i], i == 0 || entries[i - 1].who != entries[i].who,
                        i + 1 == entries.size() || entries[i + 1].who != entries[i].who, now);
  }
};

// What a mark list shows of each: the message, and for a reaction to it who
// reacted and with what.
struct mark_entry {
  message said;
  std::string event;  // the mark's own event
  std::optional<std::string> who;
  std::string key;
};

// The mentions of the user or the reactions to theirs not yet seen, as a
// list of the chat's bubbles: each its message -- a reaction's with who
// reacted and with what on a badge at its bottom right. Pressed, gone to.
template <class Actions>
struct marks_box : nodes::Stack {
  using close_act = ask<Actions, &Actions::close_marks>;
  using close_button = icon_button<close_act>;
  using top_bar = page_header<no_back, close_act>;
  struct badge : nodes::Stack {
    struct parts_t {
      avatar_mark face;
      nodes::Text key;
    } parts;
    badge(const std::string& who, const std::string& name, const std::string& key)
        : parts{.face = avatar_mark(who, name, 20.0f),
                .key = nodes::Text(key.starts_with("mxc://") ? std::string(":emoji:") : key, 15.0f, text_colour)} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.place = scene::anchor::kBottomRight, .x = -14.0f, .y = -2.0f, .autoSize = scene::axes::kBoth,
                    .padding = {2.0f, 6.0f, 2.0f, 3.0f}, .cornerRadius = 12.0f, .background = sidebar_colour});
      parts.key.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct row : nodes::Stack {
    Actions* actions = nullptr;
    mark_kind_t kind;
    std::string event;
    struct parts_t {
      message_bubble bubble;
      std::optional<badge> reacted;
    } parts;
    row(Actions* a, mark_kind_t which, const conversation& in, const mark_entry& one, const model* now)
        : actions(a), kind(which), event(one.event), parts{.bubble = message_bubble(in, one.said, true, true, now)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 12.0f, 8.0f, 12.0f},
                    .hoverBackground = chosen_colour});
      fState.setCursor(scene::cursor::hand{});
      if (one.who)
        parts.reacted.emplace(*one.who, sender_name(in, *one.who), one.key);
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->go_to_mark(kind, event);
      actions->close_marks();
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  struct parts_t {
    top_bar top;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;
  marks_box(Actions* a, mark_kind_t kind, const conversation& in, const std::vector<mark_entry>& entries, const model* now)
      : parts{.top = top_bar(splice::visit(splice::overloaded{[](mark_kind::mention) { return std::string("Mentions"); },
                                                   [](mark_kind::reaction) { return std::string("Reactions"); }},
                                kind),
                             {}, {a}, false, true)} {
    fState.apply({.fillX = true, .height = 520.0f, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    auto& flow = std::get<0>(parts.list.fChildren);
    flow.apply({.fillX = true, .autoSize = scene::axes::kY});
    auto& rows = std::get<0>(flow.fChildren);
    rows.reserve(entries.size());
    for (const mark_entry& one : entries)
      rows.emplace_back(a, kind, in, one, now);
  }
};

// The developer tools, as Element's: some JSON to read and copy; a room's
// state, by type, then by key, then the event; an event of any type sent.
template <class Actions>
struct devtools_box : nodes::Stack {
  Actions* actions = nullptr;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_devtools(); }
  };
  struct back_up {
    devtools_box* box;
    void operator()() const { box->go_back(); }
  };
  using header_t = page_header<back_up, close_it>;
  // A line of the state's list: a type, or a key of one; pressed, what is
  // under it, at the next frame -- not from inside the list it is in.
  struct pick {
    devtools_box* box;
    std::string type;
    std::optional<std::string> key;
    void operator()() const {
      box->pending = pick{box, type, key};
      scene::work::mark(box->fState.fId);  // its next frame asked for: nothing else asks
    }
  };
  struct send_press {
    devtools_box* box;
    void operator()() const { box->send(); }
  };
  using entry_row = row_item<pick>;
  using rows_t = nodes::Flow<std::vector<entry_row>>;
  struct form : nodes::Stack {
    struct parts_t {
      field type;
      field key;
      nodes::Text body_caption{"Content (a JSON object)", 13.0f, dim_colour};
      widgets::TextArea<> body;
      widgets::Button<send_press> send;
    } parts;
    explicit form(devtools_box* box)
        : parts{.type = field("Event type", "m.room.message"),
                .key = field("State key (for a state event; empty for a timeline one)", ""),
                .body = widgets::TextArea<>("{}"),
                .send = widgets::Button<send_press>("Send", {box})} {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 12.0f, 12.0f}});
      parts.body_caption.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
      parts.body.apply({.fillX = true, .height = 180.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .cornerRadius = 6.0f,
                        .background = tile_colour, .border = scene::Border{band_colour, 1.0f}});
      parts.body.setText("{\n  \n}");
      parts.send.setPrimary(true);
      parts.send.apply({.width = 120.0f, .height = 34.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    }
  };
  struct parts_t {
    header_t header;
    nodes::ScrollContainer<nodes::Text> reading{nodes::Text("", 13.0f, text_colour)};
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    std::optional<form> sending;
  } parts;
  // What it shows: some text, or the state -- all its events -- at a level.
  std::vector<change::state_entry> state;
  std::optional<std::string> type_shown;
  bool showing_state = false;
  std::optional<pick> pending;

  devtools_box(Actions* a, std::string title, std::string text)
      : actions(a), parts{.header = header_t(std::move(title), {this}, {a}, false, true)} {
    this->lay_out();
    this->show_text(std::move(text));
  }
  devtools_box(Actions* a, std::vector<change::state_entry> entries)
      : actions(a), parts{.header = header_t("Room state", {this}, {a}, false, true)}, state(std::move(entries)) {
    this->lay_out();
    this->show_types();
  }
  struct send_form_t {};
  devtools_box(Actions* a, send_form_t) : actions(a), parts{.header = header_t("Send custom event", {this}, {a}, false, true)} {
    this->lay_out();
    parts.sending.emplace(this);
    parts.reading.setVisible(false);
    parts.list.setVisible(false);
  }
  void lay_out() {
    fState.apply({.fillX = true, .height = 560.0f});
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.reading, &parts.list})
      each->apply({.fillX = true, .grow = scene::axes::kY});
    auto& text = std::get<0>(parts.reading.fChildren);
    text.setWrapped(true);
    text.setSelectable(true);
    // A margin, not padding: a text draws from its own edge.
    text.apply({.fillX = true, .margin = {6.0f, 16.0f, 12.0f, 16.0f}});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  void show_text(std::string text) {
    std::get<0>(parts.reading.fChildren).setText(std::move(text));
    parts.reading.setVisible(true);
    parts.list.setVisible(false);
    parts.reading.scrollTo(0.0f);
    this->invalidateLayout();
  }
  void show_rows(std::vector<std::pair<std::string, pick>> rows) {
    auto& all = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    all.clear();
    for (auto& [label, what] : rows)
      all.emplace_back(std::move(label), std::move(what));
    parts.reading.setVisible(false);
    parts.list.setVisible(true);
    parts.list.scrollTo(0.0f);
    this->invalidateLayout();
  }
  // Every type, and how many events of it.
  void show_types() {
    showing_state = true;
    type_shown.reset();
    parts.header.parts.back.setVisible(false);
    std::map<std::string, std::size_t> counts;
    for (const change::state_entry& one : state)
      ++counts[one.type];
    std::vector<std::pair<std::string, pick>> rows;
    for (const auto& [type, count] : counts)
      rows.emplace_back(std::format("{}  ({})", type, count), pick{this, type, std::nullopt});
    this->show_rows(std::move(rows));
  }
  // A type's keys.
  void show_keys(const std::string& type) {
    type_shown = type;
    parts.header.parts.back.setVisible(true);
    std::vector<std::pair<std::string, pick>> rows;
    for (const change::state_entry& one : state)
      if (one.type == type)
        rows.emplace_back(one.key.empty() ? std::string("(empty key)") : one.key, pick{this, type, one.key});
    this->show_rows(std::move(rows));
  }
  void go_back() {
    if (!showing_state)
      return;
    if (!parts.reading.visible() && type_shown)
      this->show_types();
    else if (type_shown)
      this->show_keys(*type_shown);
  }
  // The other button over what it reads: copied -- what is selected, or
  // all of it -- and said so in the title for a moment.
  using scene::Node::onPointer;
  void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button != 3 || !parts.reading.visible())
      return;
    const auto& text = std::get<0>(parts.reading.fChildren);
    skiff::scene::setClipboardText(text.hasSelection() ? text.selected() : text.text());
    auto& title = parts.header.parts.title;
    if (!title_before)
      title_before = title.text();
    title.setText(text.hasSelection() ? "Selection copied" : "Copied");
    title_back_at = 0.0;  // counted from the next frame
    reply.handle();
  }
  std::optional<std::string> title_before;
  double title_back_at = 0.0;
  [[nodiscard]] bool wantsTick() const { return pending.has_value() || title_before.has_value(); }
  void update(double now_ms) {
    if (title_before) {
      if (title_back_at == 0.0)
        title_back_at = now_ms + 1200.0;
      else if (now_ms >= title_back_at)
        parts.header.parts.title.setText(*std::exchange(title_before, std::nullopt));
    }
    if (!pending)
      return;
    const pick what = *std::exchange(pending, std::nullopt);
    if (!what.key) {
      this->show_keys(what.type);
      return;
    }
    for (const change::state_entry& one : state)
      if (one.type == what.type && one.key == *what.key) {
        parts.header.parts.back.setVisible(true);
        this->show_text(one.json);
        return;
      }
  }
  void send() {
    if (!parts.sending)
      return;
    auto& [type, key, caption, body, button] = parts.sending->parts;
    if (type.text().empty())
      return;
    actions->send_custom(type.text(), key.text().empty() ? std::nullopt : std::optional<std::string>(key.text()),
                         body.text());
  }
};

// A chat to forward to: its id and name.
struct forward_target {
  conversation_id id;
  std::string name;
};

// Where to forward a message, as tdesktop's box: the account's chats, with
// a field to find one by its name; a press sends it there.
template <class Actions>
struct forward_box : nodes::Stack {
  Actions* actions = nullptr;
  std::vector<forward_target> all;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_forward(); }
  };
  struct typed {
    forward_box* box;
    void operator()(std::string_view text) const { box->find(text); }
  };
  struct row : nodes::Stack {
    Actions* actions;
    conversation_id id;
    struct parts_t {
      avatar_mark face;
      nodes::Text name;
    } parts;
    row(Actions* a, const forward_target& one)
        : actions(a), id(one.id), parts{.face = avatar_mark(one.id.id, one.name, 36.0f),
                                        .name = nodes::Text(one.name, 15.0f, text_colour)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 50.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                    .hoverBackground = chosen_colour});
      fState.setCursor(scene::cursor::hand{});
      parts.name.setElided(true);
      parts.name.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->forward_to(id);
      return true;
    }
  };
  using rows_t = nodes::Flow<std::vector<row>>;
  using header_t = page_header<no_back, close_it>;
  struct parts_t {
    header_t header;
    widgets::TextBox<typed> field;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;

  forward_box(Actions* a, const std::vector<forward_target>& chats)
      : actions(a), all(chats),
        parts{.header = header_t("Forward to…", {}, {a}, false, true), .field = widgets::TextBox<typed>("Search", {this})} {
    fState.apply({.fillX = true, .height = 520.0f});
    parts.field.setSearchIcon(true);
    parts.field.apply({.fillX = true, .height = 34.0f, .margin = {0.0f, 16.0f, 8.0f, 16.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    this->find({});
  }
  // The chats whose names have what is typed, in any case.
  void find(std::string_view text) {
    const auto lower = [](std::string_view s) {
      std::string out(s);
      for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return out;
    };
    const std::string wanted = lower(text);
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    for (const forward_target& one : all)
      if (wanted.empty() || lower(one.name).contains(wanted))
        rows.emplace_back(actions, one);
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
};

// Element's Start chat (its InviteDialog, for a direct chat): who to talk
// to, found as it is typed -- among those one already has chats with, and
// in the server's user directory -- each with their picture, name and ID,
// a press on one starting the chat; and one's own link, to send to someone
// not found.
template <class Actions>
struct start_chat_box : nodes::Stack {
  Actions* actions = nullptr;
  // Those one has direct chats with, and what the directory found for what
  // is typed now; one's own link.
  std::vector<found_person> known;
  std::vector<found_person> found;
  std::string query;
  std::string link;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_new_chat(); }
  };
  struct typed {
    start_chat_box* box;
    void operator()(std::string_view text) const { box->search(text); }
  };
  struct go_press {
    start_chat_box* box;
    void operator()() const { box->go(); }
  };
  struct copy_press {
    start_chat_box* box;
    void operator()() const { box->actions->copy_text(box->link); }
  };
  // Someone found: their picture, name and ID; pressed, the chat with them.
  struct person_row : nodes::Stack {
    Actions* actions;
    std::string id;
    struct lines_t : two_lines {
      explicit lines_t(const found_person& one) : two_lines(one.name.empty() ? one.id : one.name, one.id, 14.0f, 2.0f) {}
    };
    struct parts_t {
      avatar_mark face;
      lines_t lines;
    } parts;
    person_row(Actions* a, const found_person& one)
        : actions(a), id(one.id),
          parts{.face = avatar_mark(one.id, one.name.empty() ? one.id : one.name, 36.0f), .lines = lines_t(one)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 52.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = chosen_colour});
      parts.face.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->start_direct(id);
      return true;
    }
  };
  using header_t = page_header<no_back, close_it>;
  using rows_t = nodes::Flow<std::vector<person_row>>;
  struct search_row : nodes::Stack {
    struct parts_t {
      widgets::TextBox<typed> field;
      widgets::Button<go_press> go;
    } parts;
    explicit search_row(start_chat_box* box)
        : parts{.field = widgets::TextBox<typed>("Search", {box}), .go = widgets::Button<go_press>("Go", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
      parts.field.setSearchIcon(true);
      parts.field.apply({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX,
                         .alignSelf = scene::align::kMiddle});
      parts.go.setPrimary(true);
      parts.go.apply({.width = 64.0f, .height = 34.0f, .alignSelf = scene::align::kMiddle});
    }
  };
  struct link_row : nodes::Stack {
    struct parts_t {
      nodes::Text link;
      widgets::Button<copy_press> copy;
    } parts;
    link_row(start_chat_box* box, const std::string& link)
        : parts{.link = nodes::Text(link, 13.0f, accent_colour), .copy = widgets::Button<copy_press>("Copy", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {2.0f, 10.0f, 0.0f, 10.0f}});
      parts.link.setElided(true);
      parts.link.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.copy.apply({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
    }
  };
  struct parts_t {
    header_t header;
    nodes::Text intro;
    search_row search;
    nodes::Text status;
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
    nodes::Text note;
    link_row share;
  } parts;
  start_chat_box(Actions* a, std::vector<found_person> people, std::string own_link)
      : actions(a), known(std::move(people)), link(std::move(own_link)),
        parts{.header = header_t("Start chat", {}, {a}, false, true),
              .intro = nodes::Text("Start a conversation with someone using their name or username (like @user:server).",
                                   14.0f, text_colour),
              .search = search_row(this),
              .status = nodes::Text("Suggestions", 12.0f, dim_colour, true),
              .note = nodes::Text("If you can't see who you're looking for, send them your invite link below.", 13.0f,
                                  dim_colour),
              .share = link_row(this, link)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .height = 560.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}});
    parts.intro.setWrapped(true);
    parts.intro.apply({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}});
    parts.status.apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    this->show_rows();
  }
  // Someone's whole Matrix ID, as typed: offered as it is, first.
  [[nodiscard]] static bool whole_id(std::string_view text) {
    return text.size() > 3 && text.starts_with('@') && text.find(':') != std::string_view::npos;
  }
  [[nodiscard]] static std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
  }
  void search(std::string_view text) {
    query = std::string(text);
    found.clear();
    this->show_rows();
    if (query.size() >= 2)
      actions->find_people(query);
  }
  // The directory's answer, where it is for what is typed now.
  void show_found(const std::vector<found_person>& people, const std::string& asked) {
    if (asked != query)
      return;
    found = people;
    for (const found_person& one : people)
      if (one.avatar && !one.avatar->empty())
        listed_avatars().emplace_back(one.id, *one.avatar);
    this->show_rows();
  }
  void show_rows() {
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    const std::string wanted = lower(query);
    std::set<std::string> listed;
    const auto add = [&](const found_person& one) {
      if (rows.size() < 60 && listed.insert(one.id).second)
        rows.emplace_back(actions, one);
    };
    if (whole_id(query))
      add(found_person{.id = query});
    for (const found_person& one : known)
      if (wanted.empty() || lower(one.name).contains(wanted) || lower(one.id).contains(wanted))
        add(one);
    for (const found_person& one : found)
      add(one);
    parts.status.setText(query.empty() ? "Suggestions" : rows.empty() ? "No results" : "Results");
    parts.list.invalidateLayout();
    parts.list.scrollTo(0.0f);
  }
  // Go: the ID typed, or the first found.
  void go() {
    if (whole_id(query)) {
      actions->start_direct(query);
      return;
    }
    const auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    if (!rows.empty())
      actions->start_direct(rows.front().id);
  }
};

// Element's Create a room (its CreateRoomDialog): a name, a topic, who can
// join -- by invitation, or anyone, with the address it is found by -- and,
// among the advanced, whether those of other servers may ever join.
template <class Actions>
struct create_room_box : nodes::Stack {
  Actions* actions = nullptr;
  std::string server;
  bool open_room = false;
  bool federate = true;
  bool advanced = false;
  bool choosing = false;  // the list of who can join, open
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_new_room(); }
  };
  struct create_press {
    create_room_box* box;
    void operator()() const {
      const std::string& name = box->parts.name.text();
      if (!name.empty())
        box->actions->create_room(name, box->parts.topic.text(), box->open_room, box->parts.address.text(), box->federate);
    }
  };
  struct cancel_press {
    Actions* actions;
    void operator()() const { actions->close_new_room(); }
  };
  struct flip_list {
    create_room_box* box;
    void operator()() const {
      box->choosing = !box->choosing;
      box->show_choice();
    }
  };
  struct choose_private {
    create_room_box* box;
    void operator()() const {
      box->open_room = false;
      box->choosing = false;
      box->show_choice();
    }
  };
  struct choose_public {
    create_room_box* box;
    void operator()() const {
      box->open_room = true;
      box->choosing = false;
      box->show_choice();
    }
  };
  struct flip_advanced {
    create_room_box* box;
    void operator()() const {
      box->advanced = !box->advanced;
      box->show_choice();
    }
  };
  struct flip_federate {
    create_room_box* box;
    void operator()() const {
      box->federate = !box->federate;
      box->parts.block.parts.toggle.setOn(!box->federate);  // on: blocked
    }
  };
  // Who can join, as Element's dropdown shows it: the choice and a chevron.
  struct choice_button : nodes::Stack {
    flip_list press;
    struct parts_t {
      nodes::Text value{"", 14.0f, text_colour};
      nodes::Icon chevron;
    } parts{.chevron = nodes::Icon(shape_of(icon::down{}), dim_colour)};
    explicit choice_button(create_room_box* box) : press{box} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 38.0f, .margin = {0.0f, 10.0f, 0.0f, 10.0f}, .padding = {0.0f, 12.0f, 0.0f, 12.0f},
                    .cornerRadius = 6.0f, .background = tile_colour, .hoverBackground = chosen_colour,
                    .border = scene::Border{band_colour, 1.0f}});
      parts.value.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.chevron.apply({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      press();
      return true;
    }
  };
  // One of the choices, in the open list: its name and what it means.
  template <class Choose>
  struct option_row : nodes::Stack {
    Choose choose;
    struct parts_t {
      nodes::Text name;
      nodes::Text meaning;
    } parts;
    option_row(create_room_box* box, std::string name, std::string meaning)
        : choose{box}, parts{.name = nodes::Text(std::move(name), 14.0f, text_colour, true),
                             .meaning = nodes::Text(std::move(meaning), 12.0f, dim_colour)} {
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 10.0f, 0.0f, 10.0f},
                    .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = 6.0f, .hoverBackground = chosen_colour});
      parts.meaning.setWrapped(true);
      parts.meaning.apply({.fillX = true});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      choose();
      return true;
    }
  };
  struct federate_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      widgets::Toggle<flip_federate> toggle;
    } parts;
    federate_row(create_room_box* box, const std::string& server)
        : parts{.label = nodes::Text(std::format("Block anyone not part of {} from ever joining this room.", server), 13.0f,
                                     text_colour),
                .toggle = widgets::Toggle<flip_federate>({box})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 10.0f, 4.0f, 10.0f}});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.toggle.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  using buttons_row = dialog_buttons<cancel_press, create_press>;
  using header_t = page_header<no_back, close_it>;
  struct parts_t {
    header_t header;
    field name;
    field topic;
    nodes::Text rule_caption{"Who can join", 13.0f, dim_colour};
    choice_button rule;
    option_row<choose_private> private_option;
    option_row<choose_public> public_option;
    nodes::Text rule_note{"", 13.0f, dim_colour};
    field address;
    widgets::Button<flip_advanced> show_advanced;
    federate_row block;
    nodes::Text block_note{"You might enable this if the room will only be used for collaborating with internal teams "
                           "on your homeserver. This cannot be changed later.",
                           12.0f, dim_colour};
    buttons_row buttons;
  } parts;
  create_room_box(Actions* a, std::string own_server)
      : actions(a), server(std::move(own_server)),
        parts{.header = header_t("Create a room", {}, {a}, false, true),
              .name = field("Name", ""),
              .topic = field("Topic (optional)", ""),
              .rule = choice_button(this),
              .private_option = option_row<choose_private>(this, "Private room (invite only)",
                                                           "Only people invited will be able to find and join this room."),
              .public_option = option_row<choose_public>(this, "Public room", "Anyone will be able to find and join this room."),
              .address = field("Address", std::format("#room-name:{}", server)),
              .show_advanced = widgets::Button<flip_advanced>("Show advanced", {this}),
              .block = federate_row(this, server),
              .buttons = buttons_row("Create room", {a}, {this}, 120.0f)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 18.0f, 12.0f}});
    parts.rule_caption.apply({.margin = {4.0f, 10.0f, 0.0f, 10.0f}});
    parts.rule_note.setWrapped(true);
    parts.rule_note.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.show_advanced.apply({.width = 150.0f, .height = 30.0f, .margin = {4.0f, 10.0f, 0.0f, 10.0f}});
    parts.block_note.setWrapped(true);
    parts.block_note.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    this->show_choice();
  }
  // What is shown for the choices made: the list open or not, the address
  // for a public room, the advanced part.
  void show_choice() {
    parts.header.parts.title.setText(open_room ? "Create a public room" : "Create a room");
    parts.rule.parts.value.setText(open_room ? "Public room" : "Private room (invite only)");
    parts.private_option.setVisible(choosing);
    parts.public_option.setVisible(choosing);
    parts.rule_note.setText(open_room ? "Anyone will be able to find and join this room."
                                      : "Only people invited will be able to find and join this room. You can change "
                                        "this at any time from room settings.");
    parts.address.setVisible(open_room);
    parts.show_advanced.setLabel(advanced ? "Hide advanced" : "Show advanced");
    parts.block.setVisible(advanced);
    parts.block_note.setVisible(advanced);
    this->invalidateLayout();
  }
};

// A chat's background, chosen: for every chat, an account's, or one chat
// -- as the level over it says, the theme's (its gradient and Telegram's
// pattern), a plain colour, or a picture of one's own.
// How bubbles -- or the panels -- look, at a level: as the level over it
// (where there is one), or a kind -- the one in use marked -- and how
// opaque, on a slider told when it is let go: made again once, not at each
// step of a drag. The panels' only over the background behind the whole
// window: else shown greyed, and nothing done.
template <class Actions>
struct bubbles_picker : nodes::Stack {
  // The look at the level, as the UI knows it: every chat's, or the chat's
  // shown.
  [[nodiscard]] static config::bubble_look current(const choice_level_t& level, const config::look_part_t& part) {
    const bool everywhere = splice::visit(splice::overloaded{[](choice_level::everywhere) { return true; },
                                                             [](const auto&) { return false; }},
                                          level);
    return splice::visit(splice::overloaded{[&](config::look_part::bubbles) {
                                              return everywhere ? bubble_look_everywhere() : bubble_look_now();
                                            },
                                            [&](config::look_part::panels) {
                                              return everywhere ? panel_look_everywhere() : panel_look_now();
                                            }},
                         part);
  }
  [[nodiscard]] static bool usable(const config::look_part_t& part) {
    return splice::visit(splice::overloaded{[](config::look_part::bubbles) { return true; },
                                            [](config::look_part::panels) { return window_look().behind; }},
                         part);
  }
  struct inherit_it {
    Actions* actions;
    choice_level_t level;
    config::look_part_t part;
    void operator()() const {
      if (usable(part))
        actions->set_bubbles(level, std::nullopt, part);
    }
  };
  struct pick_kind {
    Actions* actions;
    choice_level_t level;
    config::look_part_t part;
    config::bubbles_t kind;
    void operator()() const {
      if (usable(part))
        actions->set_bubbles(level, config::bubble_look{kind, current(level, part).opacity}, part);
    }
  };
  // An opacity let go at: of the kind in use -- solid has none, so
  // translucent.
  struct opacity_done {
    Actions* actions;
    choice_level_t level;
    config::look_part_t part;
    void operator()(float fraction) const {
      if (!usable(part) || !own_here(level, part))
        return;
      config::bubble_look look = current(level, part);
      look.kind = splice::visit(splice::overloaded{[](config::bubbles::solid) { return config::bubbles_t{config::bubbles::translucent{}}; },
                                                   [](const auto& other) { return config::bubbles_t{other}; }},
                                look.kind);
      look.opacity = static_cast<int>(std::lround(10.0f + std::clamp(fraction, 0.0f, 1.0f) * 90.0f));
      actions->set_bubbles(level, look, part);
    }
  };
  // An element's opacity, apart from the bubbles': let go at, or given back
  // to them.
  using element_t = std::optional<int> config::element_opacity::*;
  struct element_done {
    Actions* actions;
    choice_level_t level;
    element_t which;
    void operator()(float fraction) const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.elements.*which = static_cast<int>(std::lround(std::clamp(fraction, 0.0f, 1.0f) * 100.0f));
      actions->set_bubbles(level, look, config::look_part::bubbles{});
    }
  };
  struct element_reset {
    Actions* actions;
    choice_level_t level;
    element_t which;
    void operator()() const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.elements.*which = std::nullopt;
      actions->set_bubbles(level, look, config::look_part::bubbles{});
    }
  };
  struct element_row : nodes::Stack {
    struct head_t : label_button_row<element_reset> {
      head_t(std::string label, element_reset reset, bool own)
          : label_button_row<element_reset>(std::move(label), "As bubbles", reset, own) {}
    };
    struct parts_t {
      head_t head;
      widgets::SliderBar<scene::NoAction, element_done> bar;
    } parts;
    element_row(Actions* a, const choice_level_t& level, std::string_view name, element_t which)
        : parts{.head = head_t(std::format("{}: {}%{}", name, element_opacity_of(current(level, config::look_part::bubbles{}), which),
                                           (current(level, config::look_part::bubbles{}).elements.*which) ? "" : " (as bubbles)"),
                               element_reset{a, level, which},
                               (current(level, config::look_part::bubbles{}).elements.*which).has_value()),
                .bar = widgets::SliderBar<scene::NoAction, element_done>({}, element_done{a, level, which})} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.bar.setFraction(static_cast<float>(element_opacity_of(current(level, config::look_part::bubbles{}), which)) / 100.0f);
      parts.bar.apply({.margin = {4.0f, 8.0f, 6.0f, 8.0f}});
    }
  };
  // Whether the level has one over it to be as.
  [[nodiscard]] static bool inherits(const choice_level_t& level) {
    return splice::visit(splice::overloaded{[](choice_level::everywhere) { return false; }, [](const auto&) { return true; }}, level);
  }
  [[nodiscard]] static std::vector<std::string> kind_names(const choice_level_t& level) {
    std::vector<std::string> out;
    if (inherits(level))
      out.emplace_back(splice::visit(splice::overloaded{[](choice_level::chat) { return "As above"; },
                                                        [](const auto&) { return "As above"; }},
                                     level));
    for (const char* name : {"Solid", "Translucent", "Frosted", "Glass"})
      out.emplace_back(name);
    return out;
  }
  // The option in use: what the level holds, else as the level over it.
  [[nodiscard]] static std::size_t kind_index(const choice_level_t& level, const config::look_part_t& part) {
    const looks_held& held = looks_at(level);
    const std::optional<config::bubble_look>& own =
        splice::visit(splice::overloaded{[&](config::look_part::bubbles) -> const std::optional<config::bubble_look>& { return held.bubbles; },
                                         [&](config::look_part::panels) -> const std::optional<config::bubble_look>& { return held.panels; }},
                      part);
    const std::size_t shift = inherits(level) ? 1 : 0;
    if (!own)
      return 0;
    return shift + own->kind.index();
  }
  // Whether the level holds a look of its own: as above, what is under the
  // choice of kind is the level over it's -- shown greyed, and left alone.
  [[nodiscard]] static bool own_here(const choice_level_t& level, const config::look_part_t& part) {
    if (!inherits(level))
      return true;
    const looks_held& held = looks_at(level);
    return splice::visit(splice::overloaded{[&](config::look_part::bubbles) { return held.bubbles.has_value(); },
                                            [&](config::look_part::panels) { return held.panels.has_value(); }},
                         part);
  }
  struct pick_kind_at {
    Actions* actions;
    choice_level_t level;
    config::look_part_t part;
    bool inherit;
    void operator()(std::size_t index) const {
      if (!usable(part))
        return;
      if (inherit && index == 0) {
        actions->set_bubbles(level, std::nullopt, part);
        return;
      }
      static const std::array<config::bubbles_t, 4> kinds{config::bubbles::solid{}, config::bubbles::translucent{},
                                                         config::bubbles::frosted{}, config::bubbles::glass{}};
      const std::size_t at = index - (inherit ? 1 : 0);
      if (at >= kinds.size())
        return;
      config::bubble_look look = current(level, part);
      look.kind = kinds[at];
      actions->set_bubbles(level, look, part);
    }
  };
  // Frosted's blur, let go at: the look's own -- the bubbles' apart from the
  // panels' -- where it was let go, as it is: 55.2%, not rounded.
  struct blur_done {
    Actions* actions;
    choice_level_t level;
    config::look_part_t part;
    void operator()(float fraction) const {
      if (!usable(part) || !own_here(level, part))
        return;
      config::bubble_look look = current(level, part);
      look.blur = static_cast<double>(std::clamp(fraction, 0.0f, 1.0f)) * 100.0;
      actions->set_bubbles(level, look, part);
    }
  };
  // An element's blur, apart from the bubbles': let go at, or given back.
  using element_blur_t = std::optional<double> config::element_blur::*;
  struct element_blur_done {
    Actions* actions;
    choice_level_t level;
    element_blur_t which;
    void operator()(float fraction) const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.blurs.*which = static_cast<double>(std::clamp(fraction, 0.0f, 1.0f)) * 100.0;
      actions->set_bubbles(level, look, config::look_part::bubbles{});
    }
  };
  struct element_blur_reset {
    Actions* actions;
    choice_level_t level;
    element_blur_t which;
    void operator()() const {
      if (!own_here(level, config::look_part::bubbles{}))
        return;
      config::bubble_look look = current(level, config::look_part::bubbles{});
      look.blurs.*which = std::nullopt;
      actions->set_bubbles(level, look, config::look_part::bubbles{});
    }
  };
  // An element drawn frosted: its blur, and a way back to the bubbles'.
  struct element_blur_row : nodes::Stack {
    struct head_t : label_button_row<element_blur_reset> {
      head_t(std::string label, element_blur_reset reset, bool own)
          : label_button_row<element_blur_reset>(std::move(label), "As bubbles", reset, own) {}
    };
    struct parts_t {
      head_t head;
      widgets::SliderBar<scene::NoAction, element_blur_done> bar;
    } parts;
    element_blur_row(Actions* a, const choice_level_t& level, std::string_view name, element_blur_t which)
        : parts{.head = head_t(std::format("{} blur: {:.1f}%{}", name,
                                           element_blur_of(current(level, config::look_part::bubbles{}), which) * 100.0f,
                                           (current(level, config::look_part::bubbles{}).blurs.*which) ? "" : " (as bubbles)"),
                               element_blur_reset{a, level, which},
                               (current(level, config::look_part::bubbles{}).blurs.*which).has_value()),
                .bar = widgets::SliderBar<scene::NoAction, element_blur_done>({}, element_blur_done{a, level, which})} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.bar.setFraction(element_blur_of(current(level, config::look_part::bubbles{}), which));
      parts.bar.apply({.margin = {4.0f, 8.0f, 6.0f, 8.0f}});
    }
  };
  struct kinds_row : nodes::Stack {
    struct parts_t {
      widgets::Button<pick_kind> solid, translucent, frosted, glass;
    } parts;
    kinds_row(Actions* a, const choice_level_t& level, const config::look_part_t& part)
        : parts{.solid = widgets::Button<pick_kind>("Solid", {a, level, part, config::bubbles::solid{}}),
                .translucent = widgets::Button<pick_kind>("Translucent", {a, level, part, config::bubbles::translucent{}}),
                .frosted = widgets::Button<pick_kind>("Frosted", {a, level, part, config::bubbles::frosted{}}),
                .glass = widgets::Button<pick_kind>("Glass", {a, level, part, config::bubbles::glass{}})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      const config::bubbles_t now = current(level, part).kind;
      for (widgets::Button<pick_kind>* each : {&parts.solid, &parts.translucent, &parts.frosted, &parts.glass})
        each->apply({.height = 32.0f, .grow = scene::axes::kX, .disabled = !usable(part)});
      parts.solid.setPrimary(now == config::bubbles_t{config::bubbles::solid{}});
      parts.translucent.setPrimary(now == config::bubbles_t{config::bubbles::translucent{}});
      parts.frosted.setPrimary(now == config::bubbles_t{config::bubbles::frosted{}});
      parts.glass.setPrimary(now == config::bubbles_t{config::bubbles::glass{}});
    }
  };
  struct parts_t {
    nodes::Text title;
    nodes::Text why;
    choice_menu<pick_kind_at> kinds;
    nodes::Text opacity_label;
    widgets::SliderBar<scene::NoAction, opacity_done> opacity;
    // Frosted only: how much it blurs.
    nodes::Text blur_label;
    widgets::SliderBar<scene::NoAction, blur_done> blur;
    // The bubbles' only: what else is in a chat, each apart where chosen.
    nodes::Text elements_title{"EVERYTHING ELSE IN A CHAT", 12.0f, dim_colour, true};
    std::vector<element_row> elements;
    // Those drawn frosted, where the bubbles are: each its blur.
    std::vector<element_blur_row> element_blurs;
  } parts;
  bubbles_picker(Actions* a, const choice_level_t& level, const config::look_part_t& part = config::look_part::bubbles{})
      : parts{.title = nodes::Text(splice::visit(splice::overloaded{[](config::look_part::bubbles) { return "MESSAGE BUBBLES"; },
                                                                    [](config::look_part::panels) { return "PANELS"; }},
                                                 part),
                                   13.0f, dim_colour, true),
              .why = nodes::Text("The chat list, the bars and the side panels: only over a background behind the whole "
                                 "window (Appearance \u2192 Chat background \u2192 Behind the whole window).",
                                 12.0f, dim_colour),
              .kinds = choice_menu<pick_kind_at>("", kind_names(level), kind_index(level, part),
                                                 pick_kind_at{a, level, part, inherits(level)}),
              .opacity_label = nodes::Text("Opacity", 13.0f, text_colour),
              .opacity = widgets::SliderBar<scene::NoAction, opacity_done>({}, opacity_done{a, level, part}),
              .blur_label = nodes::Text(std::format("Blur: {:.1f}%", blur_of(current(level, part)) * 100.0f), 13.0f, text_colour),
              .blur = widgets::SliderBar<scene::NoAction, blur_done>({}, blur_done{a, level, part})} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.why.setWrapped(true);
    parts.why.apply({.fillX = true});
    parts.why.setVisible(splice::visit(splice::overloaded{[](config::look_part::panels) { return true; },
                                                          [](const auto&) { return false; }},
                                       part));
    const int opacity = current(level, part).opacity;
    parts.opacity_label.setText(std::format("Opacity: {}%", opacity));
    parts.opacity.setFraction(static_cast<float>(opacity - 10) / 90.0f);
    parts.opacity.apply({.margin = {4.0f, 8.0f, 8.0f, 8.0f}});
    const bool frosted = splice::visit(splice::overloaded{[](config::bubbles::frosted) { return true; }, [](const auto&) { return false; }},
                                       current(level, part).kind);
    parts.blur_label.setVisible(frosted);
    parts.blur.setVisible(frosted);
    parts.blur.setFraction(blur_of(current(level, part)));
    parts.blur.apply({.margin = {4.0f, 8.0f, 8.0f, 8.0f}});
    const bool bubbles = splice::visit(splice::overloaded{[](config::look_part::bubbles) { return true; },
                                                          [](const auto&) { return false; }},
                                       part);
    parts.elements_title.setVisible(bubbles);
    if (bubbles) {
      static constexpr std::array<std::string_view, 4> kLabels{"Service lines", "Images", "Avatars", "Reactions"};
      parts.elements.reserve(config::kElementNames.size());
      for (std::size_t i = 0; i < config::kElementNames.size(); ++i)
        parts.elements.emplace_back(a, level, kLabels[i], config::kElementNames[i].second);
      // Frosted: what else is drawn so, each its blur.
      if (frosted) {
        static constexpr std::array<std::string_view, 2> kBlurLabels{"Service lines", "Reactions"};
        parts.element_blurs.reserve(config::kElementBlurNames.size());
        for (std::size_t i = 0; i < config::kElementBlurNames.size(); ++i)
          parts.element_blurs.emplace_back(a, level, kBlurLabels[i], config::kElementBlurNames[i].second);
      }
    }
    // Greyed where it does nothing: all of it where the panels have nothing
    // behind them; under the kind, where the level is as above.
    if (!usable(part))
      parts.kinds.apply({.alpha = 0.4f});
    if (!usable(part) || !own_here(level, part)) {
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.opacity_label, &parts.opacity, &parts.elements_title})
        each->apply({.alpha = 0.4f, .disabled = true});
      for (element_row& each : parts.elements)
        each.apply({.alpha = 0.4f, .disabled = true});
    }
  }
};

// What a chat looks like, at a level: its background, its bubbles, and the
// panels round it -- in the dialog (every chat's, an account's) and in a
// room's Manage (its own).
template <class Actions>
struct look_choices : nodes::Stack {
  [[nodiscard]] static bool inherits(const choice_level_t& level) {
    return splice::visit(splice::overloaded{[](choice_level::everywhere) { return false; }, [](const auto&) { return true; }}, level);
  }
  struct pick_wallpaper_at {
    Actions* actions;
    choice_level_t level;
    bool inherit;
    void operator()(std::size_t index) const {
      if (inherit && index == 0) {
        actions->set_wallpaper(level, config::wallpaper_pick::inherit{});
        return;
      }
      static const std::array<config::wallpaper_pick_t, 3> picks{config::wallpaper_pick::theme{}, config::wallpaper_pick::plain{},
                                                                config::wallpaper_pick::picture{}};
      if (const std::size_t at = index - (inherit ? 1 : 0); at < picks.size())
        actions->set_wallpaper(level, picks[at]);
    }
  };
  [[nodiscard]] static std::vector<std::string> background_names(const choice_level_t& level) {
    std::vector<std::string> out;
    if (inherits(level))
      out.emplace_back(splice::visit(splice::overloaded{[](choice_level::chat) { return "As above"; },
                                                        [](const auto&) { return "As above"; }},
                                     level));
    for (const char* name : {"Theme default", "Plain colour"})
      out.emplace_back(name);
    // The picture's: by its file's name, where one is chosen here.
    out.emplace_back(splice::visit(
        splice::overloaded{[](const config::wallpaper::picture& at) {
                             return std::format("Image: {}", std::filesystem::path(at.path).filename().string());
                           },
                           [](const auto&) { return std::string("Image\u2026"); }},
        looks_at(level).wallpaper.value_or(config::wallpaper_t{config::wallpaper::theme{}})));
    return out;
  }
  [[nodiscard]] static std::size_t background_index(const choice_level_t& level) {
    const auto& own = looks_at(level).wallpaper;
    if (!own)
      return 0;
    return (inherits(level) ? 1 : 0) + own->index();
  }
  struct parts_t {
    nodes::Text background_title{"BACKGROUND", 13.0f, dim_colour, true};
    nodes::Text note;
    choice_menu<pick_wallpaper_at> background;
    bubbles_picker<Actions> bubbles;
    bubbles_picker<Actions> panels;
  } parts;
  [[nodiscard]] static std::string note_of(const choice_level_t& level) {
    const std::string where = window_look().behind ? "Behind the whole window" : "Behind the messages";
    return splice::visit(splice::overloaded{[&](choice_level::everywhere) { return where + ", in every chat."; },
                                            [&](choice_level::account) { return where + ", in this account's chats."; },
                                            [&](choice_level::chat) { return where + ", in this chat."; }},
                         level);
  }
  look_choices(Actions* a, choice_level_t level)
      : parts{.note = nodes::Text(note_of(level), 13.0f, dim_colour),
              .background = choice_menu<pick_wallpaper_at>("", background_names(level), background_index(level),
                                                           pick_wallpaper_at{a, level, inherits(level)}),
              .bubbles = bubbles_picker<Actions>(a, level, config::look_part::bubbles{}),
              .panels = bubbles_picker<Actions>(a, level, config::look_part::panels{})} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.background_title.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}});
    parts.background.apply({.margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    parts.bubbles.apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    parts.panels.apply({.margin = {10.0f, 0.0f, 0.0f, 0.0f}});
  }
};

// A chat background and its looks chosen, at a level, in a dialog.
template <class Actions>
struct wallpaper_box : nodes::Stack {
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_wallpaper(); }
  };
  using header_t = page_header<no_back, close_it>;
  struct parts_t {
    header_t header;
    look_choices<Actions> choices;
  } parts;
  wallpaper_box(Actions* a, choice_level_t level)
      : parts{.header = header_t("Chat background and looks", {}, {a}, false, true), .choices = look_choices<Actions>(a, level)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 12.0f, 18.0f, 12.0f}});
  }
};

// Threads, as Element's panel has them: in place of the chat's info, the
// room's threads -- each root, who wrote it and what, how many answers and
// when the latest came -- and one opened: its root, its answers, and a
// field to answer in it.
template <class Actions>
struct threads_panel : nodes::Stack {
  Actions* actions = nullptr;
  std::optional<std::string> open;  // the thread open, else the list
  std::vector<message> shown;       // what the open thread shows now
  std::optional<std::string> answering;  // an answer in it, answered
  // The chat it is of, as last shown: names and powers for its menus.
  const model* seen_model = nullptr;
  std::optional<conversation_id> seen_chat;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->toggle_threads(); }
  };
  struct back_it {
    Actions* actions;
    void operator()() const { actions->close_thread(); }
  };
  struct sent {
    threads_panel* panel;
    void operator()(std::string_view) const { panel->send(); }
  };
  struct send_press {
    threads_panel* panel;
    void operator()() const { panel->send(); }
  };
  struct stop_answer {
    threads_panel* panel;
    void operator()() const { panel->stop_answering(); }
  };
  // A thread in the list: its root's author and words, how many answers and
  // the latest's time; pressed, opened.
  struct thread_row : nodes::Stack {
    Actions* actions;
    std::string root;
    struct lines_t : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text said;
        nodes::Text meta;
      } parts;
      lines_t(std::string who, std::string words, std::string meta)
          : parts{.name = nodes::Text(std::move(who), 13.0f, accent_colour, true),
                  .said = nodes::Text(std::move(words), 13.0f, text_colour),
                  .meta = nodes::Text(std::move(meta), 12.0f, dim_colour)} {
        this->setGap(2.0f);
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        for (nodes::Text* each : {&parts.name, &parts.said, &parts.meta}) {
          each->setElided(true);
          each->apply({.fillX = true});
        }
      }
    };
    struct parts_t {
      avatar_mark face;
      lines_t lines;
    } parts;
    thread_row(Actions* a, const conversation& chat, const message& said)
        : actions(a), root(said.id),
          parts{.face = avatar_mark(said.sender, sender_name(chat, said.sender), 36.0f),
                .lines = lines_t(sender_name(chat, said.sender), flat(said.body.plain), meta_of(chat, said))} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = chosen_colour});
      parts.face.apply({.alignSelf = scene::align::kStart});
    }
    [[nodiscard]] static std::string flat(std::string text) {
      std::ranges::replace(text, '\n', ' ');
      return text;
    }
    // "3 replies · 12:34", the latest's author and words after it.
    [[nodiscard]] static std::string meta_of(const conversation& chat, const message& said) {
      const thread_summary summary = said.threaded.value_or(thread_summary{});
      std::string out = std::format("{} {}", summary.count, summary.count == 1 ? "reply" : "replies");
      if (!summary.last_id.empty())
        out += std::format(" · {} · {}: {}", clock_of(summary.last_at), sender_name(chat, summary.last_sender),
                           flat(summary.last_text));
      return out;
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      actions->open_thread(root);
      return true;
    }
  };
  using head_t = page_header<back_it, close_it>;
  // The chat's own composer, writing into the thread: its ✕ lets go of the
  // answer, Enter and the arrow send there, its paperclip and emoji too.
  struct in_thread {
    using cancel = stop_answer;
    using submit = sent;
    using attach = ask<Actions, &Actions::attach_in_thread>;
    using emoji = ask<Actions, &Actions::toggle_thread_emoji>;
    using send = send_press;
    static constexpr std::string_view placeholder = "Reply in thread…";
  };
  using rows_t = nodes::Flow<std::vector<thread_row>>;
  struct parts_t {
    head_t head;
    nodes::Box<> divider{band_colour};
    nodes::Text empty{"No threads here yet.", 13.0f, dim_colour};
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 2.0f, .wrap = false}, {})};
    // The thread open: the chat's own timeline, its root and answers in it
    // -- one renderer for both: runs, readers, quotes, presses,
    // menus, swipes, pictures, all as the chat has them.
    timeline_area<Actions> answers;
    composer_bar<Actions, in_thread> line;
  } parts;
  explicit threads_panel(Actions* a) : actions(a), parts{.head = head_t("Threads", {a}, {a}, false, true), .answers = timeline_area<Actions>(a), .line = composer_bar<Actions, in_thread>(a, {this}, {this}, {a}, {a}, {this})} {
    fState.apply({.fillY = true, .background = sidebar_colour});
    parts.divider.apply({.fillX = true, .height = 1.0f});
    parts.empty.apply({.margin = {16.0f, 16.0f, 0.0f, 16.0f}});
    for (auto* list : std::initializer_list<scene::Node*>{&parts.list, &parts.answers})
      list->apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 4.0f, 4.0f, 4.0f}});
    // The chat's buttons over its timeline are the chat's: not here.
    for (scene::Node* button : std::initializer_list<scene::Node*>{&parts.answers.parts.jump, &parts.answers.parts.back,
                                                                   &parts.answers.parts.mentions, &parts.answers.parts.reactions,
                                                                   &parts.answers.parts.loading})
      button->setVisible(false);
    this->setVisible(false);
  }
  // Brought up to date with the chat: its threads listed, or the one open
  // shown -- made again only where what it shows changed.
  void show(const conversation& chat, const model* now) {
    seen_model = now;
    seen_chat = chat.id;
    parts.head.parts.back.setVisible(open.has_value());
    parts.head.parts.title.setText(open ? "Thread" : "Threads");
    parts.list.setVisible(!open);
    parts.answers.setVisible(open.has_value());
    parts.line.setVisible(open.has_value());
    const auto root_of = [&](const std::string& id) { return held_message(chat, id); };
    if (!open) {
      // The roots: those the server listed, those in view with a thread,
      // and those threads are held of -- each once, the latest active first.
      std::vector<const message*> roots;
      const auto add = [&](const std::string& id) {
        if (const message* one = root_of(id); one && std::ranges::find(roots, one) == roots.end())
          roots.push_back(one);
      };
      for (const std::string& id : chat.thread_roots)
        add(id);
      for (const message& one : chat.timeline)
        if (one.threaded && one.threaded->count > 0)
          add(one.id);
      for (const auto& [id, answers] : chat.threads)
        add(id);
      const auto latest = [](const message* one) { return one->threaded ? one->threaded->last_at : one->at; };
      std::ranges::sort(roots, [&](const message* a, const message* b) { return latest(a) > latest(b); });
      auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
      rows.clear();
      for (const message* one : roots)
        rows.emplace_back(actions, chat, *one);
      parts.empty.setVisible(roots.empty());
      parts.list.invalidateLayout();
      shown.clear();
      return;
    }
    parts.empty.setVisible(false);
    std::vector<message> now_shown;
    if (const message* root = root_of(*open))
      now_shown.push_back(*root);
    if (const auto found = chat.threads.find(*open); found != chat.threads.end())
      now_shown.insert(now_shown.end(), found->second.begin(), found->second.end());
    if (now_shown == shown)
      return;
    const bool grew = now_shown.size() > shown.size();
    shown = std::move(now_shown);
    parts.answers.seen_model = now;
    parts.answers.seen_chat = chat.id;
    std::set<std::string> rooms;
    parts.answers.show_messages(chat, shown, 0, shown.size(), *now, shown_how{}, [](std::size_t) { return false; }, rooms);
    parts.answers.invalidateLayout();
    if (grew)
      parts.answers.parts.timeline.scrollToEnd(false);
  }
  [[nodiscard]] const conversation* chat_of() const { return seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr; }
  // Scrolled to an answer of the thread open, and flashed: a quote of it
  // pressed. False where it is not one of its.
  bool scroll_to(const std::string& id) {
    auto& bubbles = parts.answers.bubbles();
    const auto found = std::ranges::find(bubbles, id, &message_bubble::message_id);
    if (found == bubbles.end())
      return false;
    parts.answers.parts.timeline.scrollTo(found->bounds().fTop - 8.0f);
    found->flash.jump(1.0f);
    found->flash.setTarget(0.0f);
    found->markDamaged();
    return true;
  }
  // A message of the thread open answered from its menu, as tdesktop's
  // "Reply to <name>" over the field: in the thread, not the chat.
  void answer(std::string id, compose_context said) {
    answering = std::move(id);
    parts.line.show_context(std::move(said));
    this->invalidateLayout();
  }
  void stop_answering() {
    answering.reset();
    parts.line.show_context(std::nullopt);
    this->invalidateLayout();
  }
  // What is written, sent in the thread open -- an answer to what is
  // answered, where something is.
  void send() {
    const std::string text = parts.line.plain();
    if (!open || text.empty())
      return;
    actions->send_in_thread(*open, text, answering);
    parts.line.clear();
    this->stop_answering();
  }
};

// Emojis & Stickers, as Cinny edits them (MSC2545): the packs of a room --
// or one's own pack -- listed; a pack opened: its name, attribution and use
// (as emoji, as stickers), and its images, each with its shortcode and use,
// removable; images added from files, uploaded as they are chosen; saved as
// the room's state, or one's account data.
template <class Actions>
struct packs_box : nodes::Stack {
  Actions* actions = nullptr;
  std::optional<std::string> room;  // the room's packs, or one's own
  bool may_edit = true;
  std::vector<emote_pack> packs;  // as last listed
  emote_pack draft;               // the pack open, as it is edited
  bool open = false;              // a pack open, not the list
  bool new_pack = false;          // the one open not yet saved
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_packs(); }
  };
  struct back_press {
    packs_box* box;
    void operator()() const { box->show_list(); }
  };
  struct create_press {
    packs_box* box;
    void operator()() const { box->open_pack(std::nullopt); }
  };
  struct add_press {
    Actions* actions;
    void operator()() const { actions->pick_pack_images(); }
  };
  struct save_press {
    packs_box* box;
    void operator()() const { box->save(); }
  };
  struct delete_press {
    packs_box* box;
    void operator()() const { box->remove_pack(); }
  };
  struct flip_emoji {
    packs_box* box;
    void operator()() const {
      box->draft.emoji = !box->draft.emoji || !box->draft.sticker;
      box->show_use();
    }
  };
  struct flip_sticker {
    packs_box* box;
    void operator()() const {
      box->draft.sticker = !box->draft.sticker || !box->draft.emoji;
      box->show_use();
    }
  };
  // A pack in the list: its picture, its name, how many images and what
  // for; pressed, opened.
  struct pack_row : nodes::Stack {
    packs_box* box;
    std::size_t index;
    struct lines_t : two_lines {
      explicit lines_t(const emote_pack& one)
          : two_lines(one.name.empty() ? std::string("Unnamed pack") : one.name,
                      std::format("{} image{} · {}", one.pictures.size(), one.pictures.size() == 1 ? "" : "s",
                                  one.emoji && one.sticker ? "Emoji and stickers"
                                  : one.emoji              ? "Emoji"
                                                           : "Stickers"),
                      14.0f, 2.0f) {}
    };
    struct parts_t {
      nodes::Image<from_avatars> face;
      lines_t lines;
    } parts;
    pack_row(packs_box* b, std::size_t i, const emote_pack& one)
        : box(b), index(i),
          parts{.face = nodes::Image<from_avatars>(
                    {one.avatar.value_or(one.pictures.empty() ? std::string() : one.pictures.front().url)}),
                .lines = lines_t(one)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 56.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = chosen_colour});
      parts.face.apply({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f,
                        .background = tile_colour});
      parts.face.keepBox();
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      box->open_pack(index);
      return true;
    }
  };
  // An image of the pack open: its picture, its shortcode to edit, its use,
  // and × to take it out.
  struct picture_row : nodes::Stack {
    struct renamed {
      packs_box* box;
      std::size_t index;
      void operator()(std::string_view text) const {
        if (index < box->draft.pictures.size())
          box->draft.pictures[index].shortcode = std::string(text);
      }
    };
    struct flip_its_emoji {
      packs_box* box;
      std::size_t index;
      void operator()() const { box->flip_picture(index, true); }
    };
    struct flip_its_sticker {
      packs_box* box;
      std::size_t index;
      void operator()() const { box->flip_picture(index, false); }
    };
    struct remove_it {
      packs_box* box;
      std::size_t index;
      void operator()() const { box->remove_picture(index); }
    };
    struct parts_t {
      nodes::Image<from_avatars> face;
      widgets::TextBox<renamed> shortcode;
      nodes::Text emoji_label{"Emoji", 12.0f, dim_colour};
      widgets::Toggle<flip_its_emoji> emoji;
      nodes::Text sticker_label{"Sticker", 12.0f, dim_colour};
      widgets::Toggle<flip_its_sticker> sticker;
      icon_button<remove_it> remove;
    } parts;
    picture_row(packs_box* box, std::size_t index, const pack_picture& one)
        : parts{.face = nodes::Image<from_avatars>({one.url}),
                .shortcode = widgets::TextBox<renamed>("shortcode", {box, index}),
                .emoji = widgets::Toggle<flip_its_emoji>({box, index}),
                .sticker = widgets::Toggle<flip_its_sticker>({box, index}),
                .remove = icon_button<remove_it>(icon::close{}, {box, index})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 52.0f, .padding = {6.0f, 10.0f, 6.0f, 10.0f}});
      parts.face.apply({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                        .background = tile_colour});
      parts.face.keepBox();
      parts.shortcode.setText(one.shortcode);
      parts.shortcode.apply({.height = 32.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX,
                             .alignSelf = scene::align::kMiddle});
      parts.emoji.setOnNow(one.emoji);
      parts.sticker.setOnNow(one.sticker);
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.emoji_label, &parts.emoji, &parts.sticker_label,
                                                                   &parts.sticker, &parts.remove})
        each->apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct use_row : nodes::Stack {
    struct parts_t {
      nodes::Text emoji_label{"Use as emoji", 13.0f, text_colour};
      widgets::Toggle<flip_emoji> emoji;
      nodes::Text sticker_label{"Use as stickers", 13.0f, text_colour};
      widgets::Toggle<flip_sticker> sticker;
    } parts;
    explicit use_row(packs_box* box)
        : parts{.emoji = widgets::Toggle<flip_emoji>({box}), .sticker = widgets::Toggle<flip_sticker>({box})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 10.0f, 4.0f, 10.0f}});
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.emoji_label, &parts.emoji, &parts.sticker_label,
                                                                   &parts.sticker})
        each->apply({.alignSelf = scene::align::kMiddle});
      parts.sticker_label.apply({.margin = {0.0f, 0.0f, 0.0f, 14.0f}});
    }
  };
  struct list_buttons : nodes::Stack {
    struct parts_t {
      widgets::Button<create_press> create;
    } parts;
    explicit list_buttons(packs_box* box) : parts{.create = widgets::Button<create_press>("Create pack", {box})} {
      this->setHorizontal();
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}});
      parts.create.setPrimary(true);
      parts.create.apply({.width = 140.0f, .height = 34.0f});
    }
  };
  struct edit_buttons : nodes::Stack {
    struct parts_t {
      widgets::Button<add_press> add;
      nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
      widgets::Button<delete_press> remove;
      widgets::Button<back_press> back;
      widgets::Button<save_press> save;
    } parts;
    edit_buttons(Actions* a, packs_box* box)
        : parts{.add = widgets::Button<add_press>("Add images", {a}),
                .remove = widgets::Button<delete_press>("Delete pack", {box}),
                .back = widgets::Button<back_press>("Back", {box}),
                .save = widgets::Button<save_press>("Save", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}});
      parts.gap.apply({.height = 1.0f, .grow = scene::axes::kX});
      parts.save.setPrimary(true);
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.add, &parts.remove, &parts.back, &parts.save})
        each->apply({.width = 110.0f, .height = 34.0f});
    }
  };
  using header_t = page_header<no_back, close_it>;
  using packs_t = nodes::Flow<std::vector<pack_row>>;
  using pictures_t = nodes::Flow<std::vector<picture_row>>;
  struct parts_t {
    header_t header;
    nodes::Text note{"", 13.0f, dim_colour};
    // The list.
    nodes::ScrollContainer<packs_t> list{packs_t({.spacingY = 0.0f, .wrap = false}, {})};
    list_buttons list_actions;
    // A pack open.
    field name;
    field attribution;
    use_row use;
    nodes::Text images_heading{"Images", 13.0f, dim_colour, true};
    nodes::ScrollContainer<pictures_t> pictures{pictures_t({.spacingY = 0.0f, .wrap = false}, {})};
    edit_buttons edit_actions;
  } parts;
  packs_box(Actions* a, std::optional<std::string> in, bool editable)
      : actions(a), room(std::move(in)), may_edit(editable),
        parts{.header = header_t("Emojis & Stickers", {}, {a}, false, true),
              .list_actions = list_buttons(this),
              .name = field("Name", "Pack name"),
              .attribution = field("Attribution (optional)", "Where its images are from"),
              .use = use_row(this),
              .edit_actions = edit_buttons(a, this)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .height = 600.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.images_heading.apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    parts.pictures.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.pictures.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.note.setText("Loading…");
    this->show_list();
  }
  // What the account listed: the packs, the list shown -- one's own pack
  // opened at once, as there is one.
  void show_packs(std::vector<emote_pack> listed) {
    packs = std::move(listed);
    if (!room && !packs.empty()) {
      this->open_pack(0);
      return;
    }
    this->show_list();
  }
  void show_list() {
    open = false;
    parts.header.parts.title.setText("Emojis & Stickers");
    parts.note.setText(room ? (packs.empty() ? std::string("This room has no packs yet.")
                                             : std::string("The packs of this room: their emoji and stickers are "
                                                           "there for everyone in it."))
                            : std::string("Your own pack: its emoji and stickers are yours in every chat."));
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    for (std::size_t i = 0; i < packs.size(); ++i)
      rows.emplace_back(this, i, packs[i]);
    this->show_page();
  }
  // A pack opened -- a new one where none is named -- to edit.
  void open_pack(std::optional<std::size_t> index) {
    new_pack = !index;
    draft = index && *index < packs.size() ? packs[*index] : emote_pack{.room = room, .emoji = true, .sticker = true};
    open = true;
    parts.header.parts.title.setText(new_pack ? "New pack" : draft.name.empty() ? "Pack" : draft.name);
    parts.name.parts.box.setText(draft.name);
    parts.attribution.parts.box.setText(draft.attribution);
    parts.note.setText(may_edit ? std::string("Shortcodes are what the emoji are typed as, :like_this:.")
                                : std::string("You may not change this room's packs."));
    this->show_use();
    this->show_pictures();
    this->show_page();
  }
  void show_use() {
    parts.use.parts.emoji.setOn(draft.emoji);
    parts.use.parts.sticker.setOn(draft.sticker);
  }
  void show_pictures() {
    auto& rows = std::get<0>(std::get<0>(parts.pictures.fChildren).fChildren);
    rows.clear();
    for (std::size_t i = 0; i < draft.pictures.size(); ++i)
      rows.emplace_back(this, i, draft.pictures[i]);
    parts.pictures.invalidateLayout();
    pack_pictures_shown().clear();
    for (const pack_picture& one : draft.pictures)
      pack_pictures_shown().push_back(one.url);
  }
  // What shows: the list, or the pack open.
  void show_page() {
    parts.list.setVisible(!open);
    parts.list_actions.setVisible(!open && room.has_value() && may_edit);
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.name, &parts.attribution, &parts.use,
                                                                 &parts.images_heading, &parts.pictures, &parts.edit_actions})
      each->setVisible(open);
    parts.edit_actions.parts.add.setVisible(may_edit);
    parts.edit_actions.parts.save.setVisible(may_edit);
    parts.edit_actions.parts.remove.setVisible(may_edit && room.has_value() && !new_pack);
    parts.edit_actions.parts.back.setVisible(room.has_value());
    this->invalidateLayout();
  }
  void flip_picture(std::size_t index, bool emoji) {
    if (index >= draft.pictures.size())
      return;
    pack_picture& one = draft.pictures[index];
    // Never neither: the one flipped off turns the other on.
    if (emoji)
      one.emoji = !one.emoji || !one.sticker;
    else
      one.sticker = !one.sticker || !one.emoji;
    auto& rows = std::get<0>(std::get<0>(parts.pictures.fChildren).fChildren);
    if (index < rows.size()) {
      rows[index].parts.emoji.setOn(one.emoji);
      rows[index].parts.sticker.setOn(one.sticker);
    }
  }
  void remove_picture(std::size_t index) {
    if (index >= draft.pictures.size())
      return;
    draft.pictures.erase(draft.pictures.begin() + static_cast<std::ptrdiff_t>(index));
    removing = true;
    scene::work::mark(fState.fId);
  }
  // Rows let go at the next frame, not from inside one of their buttons.
  bool removing = false;
  [[nodiscard]] bool wantsTick() const { return removing; }
  void update(double) {
    if (!removing)
      return;
    removing = false;
    this->show_pictures();
  }
  // An image uploaded for the pack open: in it, its shortcode from its
  // file's name.
  void add_picture(pack_picture one) {
    if (!open)
      return;
    std::string code;
    for (const char c : one.shortcode)
      code += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                                                          : '_';
    if (code.empty())
      code = "image";
    std::string unique = code;
    for (int n = 2; std::ranges::contains(draft.pictures, unique, &pack_picture::shortcode); ++n)
      unique = std::format("{}_{}", code, n);
    one.shortcode = unique;
    one.emoji = draft.emoji;
    one.sticker = draft.sticker;
    draft.pictures.push_back(std::move(one));
    this->show_pictures();
  }
  void save() {
    draft.name = parts.name.text();
    draft.attribution = parts.attribution.text();
    if (draft.state_key.empty() && room) {
      std::string key;
      for (const char c : draft.name)
        key += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                                                           : '_';
      draft.state_key = key.empty() ? std::string("pack") : key;
    }
    if (!draft.avatar && !draft.pictures.empty())
      draft.avatar = draft.pictures.front().url;
    std::erase_if(draft.pictures, [](const pack_picture& one) { return one.shortcode.empty() || one.url.empty(); });
    actions->save_pack(draft);
    parts.note.setText("Saving…");
  }
  void remove_pack() {
    if (!room)
      return;
    actions->delete_pack(*room, draft.state_key);
    parts.note.setText("Deleting…");
  }
};

// Element's Explore rooms: a server's public directory, searched -- one's
// own, or another named -- each room with its picture, name, address, how
// many are in it and what it is about, and Join. An address typed in is
// gone to at once.
template <class Actions>
struct explore_box : nodes::Stack {
  Actions* actions = nullptr;
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_explore(); }
  };
  struct search_press {
    explore_box* box;
    void operator()() const {
      if (box->space) {
        box->filter(box->parts.search.parts.query.text());
        return;
      }
      box->parts.status.setText("Searching\u2026");
      box->parts.status.setVisible(true);
      box->actions->search_rooms(box->parts.search.parts.server.text(), box->parts.search.parts.query.text());
    }
  };
  // Whose rooms are listed, where a space's are; and what it listed, to be
  // searched here -- its server searches no space.
  std::optional<std::string> space;
  std::vector<directory_room> listed;
  std::string listed_server;
  // A space's name and picture, over what it holds.
  struct space_head_t : nodes::Stack {
    struct parts_t {
      std::optional<avatar_mark> face;
      nodes::Text name{"", 17.0f, text_colour, true};
    } parts;
    space_head_t() {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 10.0f, 8.0f, 10.0f}});
      parts.name.setElided(true);
      parts.name.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  // Join a room; a space in a space's listing, opened -- its own listed.
  struct join_press {
    Actions* actions;
    std::string room;
    std::string server;
    bool open = false;
    std::string name;
    void operator()() const {
      if (open)
        actions->explore_space(room, name);
      else
        actions->join_directory_room(room, server);
    }
  };
  struct result_row : nodes::Stack {
    struct texts_t : nodes::Stack {
      struct parts_t {
        nodes::Text name;
        nodes::Text line;
        nodes::Text topic;
      } parts;
      explicit texts_t(const directory_room& one)
          : parts{.name = nodes::Text(one.name.empty() ? (one.alias.empty() ? one.id : one.alias) : one.name, 14.0f,
                                      text_colour, true),
                  .line = nodes::Text(std::format("{}{}{} member{}", one.alias, one.alias.empty() ? "" : " \u00b7 ",
                                                  one.members, one.members == 1 ? "" : "s"),
                                      12.0f, dim_colour),
                  .topic = nodes::Text(one.topic, 13.0f, text_colour)} {
        this->setGap(2.0f);
        fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .shrink = scene::axes::kX,
                      .alignSelf = scene::align::kMiddle});
        parts.name.setElided(true);
        parts.line.setElided(true);
        parts.topic.setElided(true);
        parts.topic.setVisible(!one.topic.empty());
        for (nodes::Text* each : {&parts.name, &parts.line, &parts.topic})
          each->apply({.fillX = true});
      }
    };
    struct parts_t {
      avatar_mark face;
      texts_t texts;
      widgets::Button<join_press> join;
    } parts;
    result_row(Actions* a, const directory_room& one, const std::string& server)
        : parts{.face = avatar_mark(one.id, one.name.empty() ? one.alias : one.name, 40.0f),
                .texts = texts_t(one),
                .join = widgets::Button<join_press>(one.space ? "Open" : "Join",
                                                    {a, one.space ? one.id : (one.alias.empty() ? one.id : one.alias), server, one.space,
                                                     one.name})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 16.0f, 8.0f, 16.0f}});
      parts.join.setPrimary(true);
      parts.join.apply({.width = 70.0f, .height = 30.0f, .alignSelf = scene::align::kMiddle});
    }
  };
  using header_t = page_header<no_back, close_it>;
  struct search_row : nodes::Stack {
    struct parts_t {
      field query;
      field server;
      widgets::Button<search_press> search;
    } parts;
    search_row(explore_box* box, const std::string& own)
        : parts{.query = field("Find a room", "Name, topic, or #address:server"),
                .server = field("Server", own, own),
                .search = widgets::Button<search_press>("Search", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 6.0f, 0.0f, 6.0f}});
      // Their full-width default let go -- fillX = false says nothing -- or
      // the query, as wide as the row and growing too, lost its growing and
      // pushed the rest out past the dialog's right edge.
      parts.query.apply({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX});
      parts.server.apply({.width = 170.0f, .relativeSize = scene::axes::kNone});
      parts.search.setPrimary(true);
      parts.search.apply({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd,
                          .margin = {0.0f, 0.0f, 2.0f, 0.0f}});
    }
  };
  using rows_t = nodes::Flow<std::vector<result_row>>;
  struct parts_t {
    header_t header;
    space_head_t space_head;
    search_row search;
    nodes::Text status{"", 13.0f, dim_colour};
    nodes::ScrollContainer<rows_t> list{rows_t({.spacingY = 0.0f, .wrap = false}, {})};
  } parts;
  // The row's fields, by their names, for what reads them.
  field& query_field() { return parts.search.parts.query; }
  explore_box(Actions* a, const std::string& own_server)
      : actions(a), parts{.header = header_t("Explore rooms", {}, {a}, false, true), .search = search_row(this, own_server)} {
    fState.apply({.fillX = true, .height = 560.0f, .padding = {0.0f, 12.0f, 12.0f, 12.0f}});
    parts.status.setWrapped(true);
    parts.status.apply({.fillX = true, .margin = {6.0f, 10.0f, 4.0f, 10.0f}});
    parts.status.setVisible(false);
    parts.space_head.setVisible(false);
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  // A space's listing: its name and picture on top, no server to ask --
  // the search looks through what it holds.
  void as_space(const std::string& room, const std::string& name) {
    space = room;
    parts.space_head.parts.face.emplace(room, name, 36.0f);
    parts.space_head.parts.name.setText(name);
    parts.space_head.setVisible(true);
    parts.search.parts.server.setVisible(false);
    this->invalidateLayout();
  }
  // What it listed, whose name, topic or address has what is typed.
  void filter(const std::string& typed) {
    const auto lower = [](std::string_view text) {
      return text | std::views::transform([](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }) |
             std::ranges::to<std::string>();
    };
    const std::string wanted = lower(typed);
    const std::vector<directory_room> found =
        listed | std::views::filter([&](const directory_room& one) {
          return wanted.empty() || lower(one.name).contains(wanted) || lower(one.topic).contains(wanted) ||
                 lower(one.alias).contains(wanted);
        }) |
        std::ranges::to<std::vector>();
    this->show_rows(found, listed_server, space);
  }
  // What the directory listed -- or a space.
  void show(const std::vector<directory_room>& rooms, const std::string& server,
            const std::optional<std::string>& space_of = std::nullopt) {
    listed = rooms;
    listed_server = server;
    this->show_rows(rooms, server, space_of);
  }
  void show_rows(const std::vector<directory_room>& rooms, const std::string& server,
                 const std::optional<std::string>& space) {
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    rows.reserve(rooms.size());
    for (const directory_room& one : rooms)
      rows.emplace_back(actions, one, server);
    // Their pictures, asked for as a chat's are.
    for (const directory_room& one : rooms)
      if (one.avatar && !one.avatar->empty())
        listed_avatars().emplace_back(one.id, *one.avatar);
    parts.status.setText(space ? (rooms.empty() ? std::string("Nothing in this space, or its server would not say.")
                                                : std::format("{} rooms and spaces in this space", rooms.size()))
                               : rooms.empty() ? std::string("No rooms found.")
                                               : std::format("{} rooms", rooms.size()));
    parts.status.setVisible(true);
    this->invalidateLayout();
  }
};

// What the management of a room shows: as it is now.
[[nodiscard]] inline std::string role_of(std::int64_t level) {
  if (level >= 100)
    return "Admin";
  if (level >= 50)
    return "Moderator";
  return level > 0 ? std::format("Level {}", level) : std::string("Member");
}

template <class Actions>
struct info_panel : nodes::Stack {
  Actions* actions = nullptr;
  account_id account;
  std::string key;
  // The member shown on a page of their own, over the group's, if one is:
  // the panel's own state, which the page is made from.
  std::optional<std::string> person;
  // The members as last shown, and how each is.
  std::vector<std::pair<member, std::string>> shown_members;
  // Whose members those are, at which revision of them.
  std::optional<conversation_id> members_of;
  std::uint64_t members_revision = 0;

  // What a press does, to the panel -- which stays where it is while its
  // pages are made again.
  struct open_person {
    info_panel* panel;
    void operator()(const auto& row) const { panel->actions->open_member_info(row.id); }
  };
  struct back_to_group {
    info_panel* panel;
    void operator()() const { panel->close_member(); }
  };
  struct message_them {
    Actions* actions;
    info_panel* panel;
    void operator()() const {
      if (panel->person)
        actions->message_person(conversation_id{panel->account, *panel->person});
    }
  };

  // What the upper part shows: a chat's, or one of its members'.
  struct view {
    std::string key;
    std::string name;
    std::string status;
    bool group = false;
    bool muted = false;
    bool of_person = false;
    // What copying the ID gives: for a Matrix room, a link to it with the
    // servers to join through; else the ID.
    std::string copied;
    // What it is about, and the addresses it publishes: shown as
    // Telegram shows a group's description and its link.
    std::string topic;
    std::vector<std::string> addresses;
    friend bool operator==(const view&, const view&) = default;
  };

  // The upper part, made from its view: ← where a member is shown, ✕; the
  // big avatar, the name, how it is; the chat's tiles or the member's; its ID.
  struct head : nodes::Stack {
    struct top_row : nodes::Stack {
      using close_button = icon_button<ask<Actions, &Actions::toggle_info>>;
      struct parts_t {
        icon_button<back_to_group> back;
        nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
        close_button close;
      } parts;
      top_row(Actions* a, info_panel* panel, bool with_back)
          : parts{.back = icon_button<back_to_group>(icon::back{}, {panel}), .close = close_button(icon::close{}, {a})} {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 0.0f, 8.0f}});
        parts.gap.apply({.height = 1.0f, .grow = scene::axes::kX});
        parts.back.setVisible(with_back);
      }
    };
    struct tiles_row : nodes::Stack {
      using mute_tile = action_tile<ask<Actions, &Actions::toggle_mute>>;
      using manage_tile = action_tile<ask<Actions, &Actions::open_manage>>;
      using leave_tile = action_tile<ask<Actions, &Actions::leave_chat>>;
      struct parts_t {
        mute_tile mute;
        manage_tile manage;
        leave_tile leave;
      } parts;
      tiles_row(Actions* a, bool muted)
          : parts{.mute = mute_tile(muted ? "Unmute" : "Mute", icon::bell{}, {a}),
                  .manage = manage_tile("Manage", icon::sliders{}, {a}),
                  .leave = leave_tile("Leave", icon::leave{}, {a})} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        parts.mute.apply({.grow = scene::axes::kX});
        parts.manage.apply({.grow = scene::axes::kX});
        parts.leave.apply({.grow = scene::axes::kX});
      }
    };
    // A member's own: a message to them.
    struct person_row : nodes::Stack {
      struct parts_t {
        action_tile<message_them> message;
      } parts;
      person_row(Actions* a, info_panel* panel)
          : parts{.message = action_tile<message_them>("Message", icon::send{}, {a, panel})} {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        parts.message.apply({.grow = scene::axes::kX});
      }
    };
    // What the chat is about, as Telegram's group description: its text,
    // links in it pressed as any, and what it is under it, dim.
    struct about_block : nodes::Stack {
      struct parts_t {
        nodes::Text text;
        nodes::Text label{"Description", 12.0f, dim_colour};
      } parts;
      explicit about_block(const std::string& said) : parts{.text = nodes::Text(said, 14.0f, text_colour)} {
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}});
        parts.text.setWrapped(true);
        parts.text.setSelectable(true);
        parts.text.setLinks(link_spans_in(said), accent_colour);
        parts.text.apply({.fillX = true});
      }
    };
    struct parts_t {
      top_row top;
      avatar_button<Actions> avatar;
      nodes::Text name;
      nodes::Text status;
      std::optional<tiles_row> tiles;
      std::optional<person_row> person_tiles;
      nodes::Box<> band_1 = section_band();
      about_block about;
      std::vector<id_line> addresses;
      id_line id_text;
    } parts;

    head(Actions* a, info_panel* panel, const view& shown)
        : parts{.top = top_row(a, panel, shown.of_person),
                .avatar = avatar_button<Actions>(a, shown.key, shown.name, 96.0f),
                .name = nodes::Text(shown.name, 17.0f, text_colour, true),
                .status = nodes::Text(shown.status, 13.0f, dim_colour),
                .about = about_block(shown.topic),
                .id_text = id_line(shown.key, shown.copied)} {
      auto& [top, avatar, name, status, tiles, person_tiles, band_1, about, addresses, id_text] = parts;
      about.setVisible(!shown.topic.empty());
      for (const std::string& address : shown.addresses)
        addresses.emplace_back(address, "", "Address");
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      if (shown.of_person)
        person_tiles.emplace(a, panel);
      else
        tiles.emplace(a, shown.muted);
      for (nodes::Text* centred : {&name, &status}) {
        centred->setElided(true);
        centred->apply({.alignSelf = scene::align::kMiddle, .margin = {4.0f, 20.0f, 0.0f, 20.0f}});
        centred->setSelectable(true);
      }
    }
  };
  struct members_head : nodes::Stack {
    using add_button = icon_button<not_yet<Actions>>;
    struct parts_t {
      icon_view people{icon::people{}};
      nodes::Text title;
      add_button add_member;
    } parts;
    members_head(Actions* a, std::size_t count)
        : parts{.title = nodes::Text(std::format("{} MEMBER{}", count, count == 1 ? "" : "S"), 13.0f, dim_colour, true),
                .add_member = add_button(icon::add_person{}, {a, "Adding members"})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fill = true, .padding = {6.0f, 10.0f, 6.0f, 16.0f}});
      parts.title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  using member_rows = nodes::Flow<std::vector<member_row<open_person>>>;
  // All of it in one column that scrolls, as Telegram's profile: the head
  // -- however long its description and addresses -- then the members.
  struct column : nodes::Stack {
    struct parts_t {
      nodes::Memo<view, head> upper;
      nodes::Box<> band_2 = section_band();
      // The members' head, as a function of how many there are.
      nodes::Memo<std::size_t, members_head> members_header;
      // The members, reconciled: the rows kept while they show the same.
      member_rows members{{.spacingY = 0.0f, .wrap = false}, {}};
    } parts;
    column() {
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  struct parts_t {
    nodes::ScrollContainer<column> scroll{column()};
    nodes::Box<> edge{band_colour};  // its left edge
  } parts;
  column& content = std::get<0>(parts.scroll.fChildren);
  nodes::Memo<view, head>& upper = content.parts.upper;
  nodes::Box<>& band_2 = content.parts.band_2;
  nodes::Memo<std::size_t, members_head>& members_header = content.parts.members_header;
  member_rows& members = content.parts.members;
  // The group's view, to come back to from a member's page.
  view group_view;

  static constexpr float kWidth = 340.0f;

  explicit info_panel(Actions* a) : actions(a) {
    fState.apply({.background = sidebar_colour, .masking = true});
    parts.edge.apply({.place = scene::anchor::kTopLeft, .fillY = true, .width = 1.0f});
    parts.scroll.apply({.fillX = true, .grow = scene::axes::kY});
    upper.apply({.fillX = true, .autoSize = scene::axes::kY});
    members_header.apply({.fillX = true, .height = 48.0f});
    members.apply({.fillX = true, .autoSize = scene::axes::kY});
  }

  // The chat shown: its view worked out, its members reconciled.
  void show(const conversation& one, const model& now, bool muted) {
    if (one.id.id != key || one.id.account != account)
      person.reset();
    key = one.id.id;
    account = one.id.account;
    const bool group = is_group(one);
    group_view = {one.id.id,
                  display_name(one),
                  group ? std::format("{} member{}", std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count),
                                      std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count) == 1 ? "" : "s")
                        : presence_of(now, one.id.account, contact_of(one)),
                  group,
                  muted,
                  false};
    if (group && is_matrix(one.id.account.speaks))
      group_view.copied = logic::room_link(one);
    group_view.topic = one.topic.value_or("");
    if (one.alias)
      group_view.addresses.push_back(*one.alias);
    std::ranges::copy(one.other_aliases, std::back_inserter(group_view.addresses));
    // Its members made again only where they changed -- or another chat's
    // are shown: a big room has thousands, and every refresh rebuilt them.
    const bool same_members = members_of == one.id && members_revision == one.members_revision;
    members_of = one.id;
    members_revision = one.members_revision;
    if (!same_members) {
      shown_members.clear();
      for (const member& each : one.members) {
        // A Matrix room's say as their role, where it gives them one, as
        // Element marks its admins and moderators.
        member shown = each;
        if (!shown.role)
          if (const auto level = one.powers.find(each.id); level != one.powers.end() && level->second >= 50)
            shown.role = role_of(level->second);
        shown_members.emplace_back(std::move(shown), presence_of(now, one.id.account, each.id));
      }
    }
    auto& rows = std::get<0>(members.fChildren);
    if (!same_members && nodes::reconcile(
            rows, shown_members, [](const auto& each) { return each.first.id; },
            [](const member_row<open_person>& row) { return row.id; },
            [&](const auto& each) { return member_row<open_person>(each.first, each.second, open_person{this}); },
            [](const member_row<open_person>& row, const auto& each) {
              return row.who == each.first && row.how_shown == each.second;
            }))
      members.invalidateLayout();
    members_header.show(static_cast<std::size_t>(std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count)),
                        [this](std::size_t count) { return members_head(actions, count); });
    this->render();
  }
  void open_member(std::string id) {
    person = std::move(id);
    this->render();
  }
  void close_member() {
    person.reset();
    this->render();
  }
  // The upper part as a function of the group's view and the member open.
  void render() {
    view shown = group_view;
    if (person) {
      // Anyone's page: a member's, with how they are and their role; or,
      // for someone the chat does not list -- the other side of a direct
      // chat, a sender from further back -- by their address.
      shown = {*person, *person, std::string("not a member of this chat"), group_view.group, group_view.muted, true};
      if (const auto found = std::ranges::find(shown_members, *person, [](const auto& each) { return each.first.id; });
          found != shown_members.end()) {
        const member& who = found->first;
        shown = {who.id,
                 who.name.empty() ? who.id : who.name,
                 who.role ? std::format("{} · {}", found->second, *who.role) : found->second,
                 group_view.group,
                 group_view.muted,
                 true};
      } else if (!group_view.group && *person == key) {
        shown.name = group_view.name;
        shown.status = group_view.status;
      }
    }
    upper.show(shown, [this](const view& v) { return head(actions, this, v); });
    const bool list = shown.group && !shown.of_person;
    band_2.setVisible(list);
    members_header.setVisible(list);
    members.setVisible(list);
    this->invalidateLayout();
  }

};

}  // namespace mux::ui
