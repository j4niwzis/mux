// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info -- A chat's info, and a person's.
export module mux.ui:info;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import mux.core;
import mux.config;
import mux.logic.links;
import :base;
import :icons;
import :controls;
import :themes;
import :names;

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
  struct texts_column : nodes::Stack {
    struct parts_t {
      nodes::Text name;
      nodes::Text state;
    } parts;
    texts_column(std::string shown, std::string how)
        : parts{.name = nodes::Text(std::move(shown), 14.0f, text_colour, true),
                .state = nodes::Text(std::move(how), 12.0f, dim_colour)} {
      this->setGap(4.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&parts.name, &parts.state}) {
        each->setElided(true);
        each->apply({.fillX = true});
      }
    }
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
  id_line(std::string text, std::string link)
      : parts{.id = nodes::Text(text, 14.0f, accent_colour)}, copied(link.empty() ? text : link), a_link(!link.empty()) {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
    fState.setCursor(scene::cursor::hand{});
    parts.id.setWrapped(true);
    parts.id.apply({.fillX = true});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    skiff::scene::setClipboardText(copied);
    parts.label.setText(a_link ? "ID · link copied, with its servers" : "ID · copied");
    return true;
  }
};

// A person's name and how they are, as a chat knows them: a member's, with
// their role; the other side of a direct chat; anyone else by their address.
struct person_facts {
  std::string name;
  std::string status;
};
[[nodiscard]] inline person_facts person_of(const conversation* in, const model& now, const account_id& account,
                                            const std::string& id) {
  person_facts out{id, presence_of(now, account, id)};
  if (in == nullptr)
    return out;
  if (const auto found = std::ranges::find(in->members, id, &member::id); found != in->members.end()) {
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
  using close_button = icon_button<ask<Actions, &Actions::close_person_info>>;
  struct top_bar : nodes::Stack {
    struct parts_t {
      nodes::Text title{"User info", 16.0f, text_colour, true};
      close_button close;
    } parts;
    explicit top_bar(Actions* a) : parts{.close = close_button(icon::close{}, {a})} {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 10.0f, 0.0f, 22.0f}});
      parts.title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.close.apply({.alignSelf = scene::align::kMiddle});
    }
  };
  // tdesktop's cover: 108 high, a 72 photo, the name and status beside it.
  struct cover : nodes::Stack {
    struct parts_t {
      avatar_mark photo;
      two_lines texts;
    } parts;
    cover(const std::string& key, const person_facts& facts)
        : parts{.photo = avatar_mark(key, facts.name, 72.0f), .texts = two_lines(facts.name, facts.status, 17.0f, 6.0f)} {
      this->setHorizontal();
      this->setGap(16.0f);
      fState.apply({.fillX = true, .height = 108.0f, .padding = {0.0f, 22.0f, 0.0f, 22.0f}});
    }
  };
  struct parts_t {
    top_bar top;
    cover face;
    nodes::Box<> band = section_band();
    id_line id;
    action_tile<message_them> message;
  } parts;

  person_card(Actions* a, const account_id& account, const std::string& key, const person_facts& facts)
      : parts{.top = top_bar(a),
              .face = cover(key, facts),
              .id = id_line(key, ""),
              .message = action_tile<message_them>("Message", icon::send{}, {a, conversation_id{account, key}})} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 16.0f, 0.0f}});
    parts.message.apply({.fillX = true, .margin = {8.0f, 22.0f, 0.0f, 22.0f}});
  }
};

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
      using manage_tile = action_tile<not_yet<Actions>>;
      using leave_tile = action_tile<ask<Actions, &Actions::leave_chat>>;
      struct parts_t {
        mute_tile mute;
        manage_tile manage;
        leave_tile leave;
      } parts;
      tiles_row(Actions* a, bool muted)
          : parts{.mute = mute_tile(muted ? "Unmute" : "Mute", icon::bell{}, {a}),
                  .manage = manage_tile("Manage", icon::sliders{}, {a, "Managing a chat"}),
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
    struct parts_t {
      top_row top;
      big_avatar avatar;
      nodes::Text name;
      nodes::Text status;
      std::optional<tiles_row> tiles;
      std::optional<person_row> person_tiles;
      nodes::Box<> band_1 = section_band();
      id_line id_text;
    } parts;

    head(Actions* a, info_panel* panel, const view& shown)
        : parts{.top = top_row(a, panel, shown.of_person),
                .name = nodes::Text(shown.name, 17.0f, text_colour, true),
                .status = nodes::Text(shown.status, 13.0f, dim_colour),
                .id_text = id_line(shown.key, shown.copied)} {
      auto& [top, avatar, name, status, tiles, person_tiles, band_1, id_text] = parts;
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      avatar.show(shown.key, shown.name);
      if (shown.of_person)
        person_tiles.emplace(a, panel);
      else
        tiles.emplace(a, shown.muted);
      for (nodes::Text* centred : {&name, &status}) {
        centred->setElided(true);
        centred->apply({.alignSelf = scene::align::kMiddle, .margin = {4.0f, 20.0f, 0.0f, 20.0f}});
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
  using member_list = nodes::ScrollContainer<nodes::Flow<std::vector<member_row<open_person>>>>;
  struct parts_t {
    nodes::Memo<view, head> upper;
    nodes::Box<> band_2 = section_band();
    // The members' head, as a function of how many there are.
    nodes::Memo<std::size_t, members_head> members_header;
    // The members, in a list of their own that scrolls, reconciled: its
    // place and its rows kept while they show the same.
    member_list members{nodes::Flow<std::vector<member_row<open_person>>>({.spacingY = 0.0f, .wrap = false}, {})};
    nodes::Box<> edge{band_colour};  // its left edge
  } parts;
  nodes::Memo<view, head>& upper = parts.upper;
  nodes::Box<>& band_2 = parts.band_2;
  nodes::Memo<std::size_t, members_head>& members_header = parts.members_header;
  member_list& members = parts.members;
  // The group's view, to come back to from a member's page.
  view group_view;

  static constexpr float kWidth = 340.0f;

  explicit info_panel(Actions* a) : actions(a) {
    fState.apply({.background = sidebar_colour, .masking = true});
    parts.edge.apply({.place = scene::anchor::kTopLeft, .fillY = true, .width = 1.0f});
    this->setGap(2.0f);
    upper.apply({.fillX = true, .autoSize = scene::axes::kY});
    members_header.apply({.fillX = true, .height = 48.0f});
    members.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(members.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
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
    // Its members made again only where they changed -- or another chat's
    // are shown: a big room has thousands, and every refresh rebuilt them.
    const bool same_members = members_of == one.id && members_revision == one.members_revision;
    members_of = one.id;
    members_revision = one.members_revision;
    if (!same_members) {
      shown_members.clear();
      for (const member& each : one.members)
        shown_members.emplace_back(each, presence_of(now, one.id.account, each.id));
    }
    auto& rows = std::get<0>(std::get<0>(members.fChildren).fChildren);
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
