// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info -- A chat's info, and a person's.
export module mux.ui:info;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :header;

export namespace mux::ui {

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
struct action_tile : nodes::Stack {
  Act act;
  icon_mark mark;
  nodes::Text label;

  // Declared: the icon at the top, the name at the bottom.
  action_tile(std::string text, icon_t icon, Act what = {})
      : act(std::move(what)), mark(icon), label(std::move(text), 12.0f, text_colour) {
    fState.apply({.height = 58.0f, .padding = {6.0f, 0.0f, 8.0f, 0.0f}, .cornerRadius = 8.0f, .background = tile_colour, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
    fStack.justify = nodes::justify::space_between{};
    mark.setColour(text_colour);
    mark.apply({.height = 24.0f});
    label.apply({.alignSelf = scene::align::kMiddle});
  }

  void forEachChild(auto&& f) {
    f(mark);
    f(label);
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
    out.fLabel = label.text();
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
  avatar_mark face;
  // Their name, and how they are under it.
  struct texts_column : nodes::Stack {
    nodes::Text name;
    nodes::Text state;
    texts_column(std::string shown, std::string how)
        : name(std::move(shown), 14.0f, text_colour, true), state(std::move(how), 12.0f, dim_colour) {
      this->setGap(4.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&name, &state}) {
        each->setElided(true);
        each->apply({.fillX = true});
      }
    }
    void forEachChild(auto&& f) {
      f(name);
      f(state);
    }
  } texts;
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
  } pill;

  // Declared: the avatar, the name over how they are, the role at the end.
  member_row(const member& one, std::string how, Open what)
      : who(one), how_shown(how), open(std::move(what)), id(one.id), role(one.role),
        face(one.id, one.name.empty() ? one.id : one.name, 40.0f),
        texts(one.name.empty() ? one.id : one.name, std::move(how)), pill(one.role.value_or("")) {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}, .hoverBackground = chosen_colour});
    pill.setVisible(one.role.has_value());
  }

  void forEachChild(auto&& f) {
    f(face);
    f(texts);
    f(pill);
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
template <class Actions>
struct info_panel : nodes::Stack {
  nodes::Box<> edge{band_colour};  // its left edge
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
    void operator()(const auto& row) const { panel->open_member(row.id); }
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
    friend bool operator==(const view&, const view&) = default;
  };

  // The upper part, made from its view: ← where a member is shown, ✕; the
  // big avatar, the name, how it is; the chat's tiles or the member's; its ID.
  struct head : nodes::Stack {
    struct top_row : nodes::Stack {
      icon_button<back_to_group> back;
      nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
      icon_button<ask<Actions, &Actions::toggle_info>> close;
      top_row(Actions* a, info_panel* panel, bool with_back) : back(icon::back{}, {panel}), close(icon::close{}, {a}) {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 0.0f, 8.0f}});
        gap.apply({.height = 1.0f, .grow = scene::axes::kX});
        back.setVisible(with_back);
      }
      void forEachChild(auto&& f) {
        f(back);
        f(gap);
        f(close);
      }
    } top;
    big_avatar avatar;
    nodes::Text name;
    nodes::Text status;
    struct tiles_row : nodes::Stack {
      action_tile<ask<Actions, &Actions::toggle_mute>> mute;
      action_tile<not_yet<Actions>> manage;
      action_tile<ask<Actions, &Actions::leave_chat>> leave;
      tiles_row(Actions* a, bool muted)
          : mute(muted ? "Unmute" : "Mute", icon::bell{}, {a}), manage("Manage", icon::sliders{}, {a, "Managing a chat"}),
            leave("Leave", icon::leave{}, {a}) {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        mute.apply({.grow = scene::axes::kX});
        manage.apply({.grow = scene::axes::kX});
        leave.apply({.grow = scene::axes::kX});
      }
      void forEachChild(auto&& f) {
        f(mute);
        f(manage);
        f(leave);
      }
    };
    // A member's own: a message to them.
    struct person_row : nodes::Stack {
      action_tile<message_them> message;
      person_row(Actions* a, info_panel* panel) : message("Message", icon::send{}, {a, panel}) {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        message.apply({.grow = scene::axes::kX});
      }
      void forEachChild(auto&& f) { f(message); }
    };
    std::optional<tiles_row> tiles;
    std::optional<person_row> person_tiles;
    nodes::Box<> band_1 = section_band();
    // The ID, whole -- wrapped, never cut -- and copied when pressed.
    struct id_line : nodes::Stack {
      nodes::Text id;
      nodes::Text label{"ID", 12.0f, dim_colour};
      explicit id_line(std::string text) : id(std::move(text), 14.0f, accent_colour) {
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}, .hoverBackground = chosen_colour, .focusBackground = chosen_colour});
        fState.setCursor(scene::cursor::hand{});
        id.setWrapped(true);
        id.apply({.fillX = true});
      }
      void forEachChild(auto&& f) {
        f(id);
        f(label);
      }
      [[nodiscard]] bool acceptsInput() const { return true; }
      [[nodiscard]] bool hoverChangesAppearance() const { return true; }
      [[nodiscard]] bool onClick(float, float) {
        skiff::scene::setClipboardText(id.text());
        label.setText("ID · copied");
        return true;
      }
    } id_text;

    head(Actions* a, info_panel* panel, const view& shown)
        : top(a, panel, shown.of_person), name(shown.name, 17.0f, text_colour, true),
          status(shown.status, 13.0f, dim_colour), id_text(shown.key) {
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
    void forEachChild(auto&& f) {
      f(top);
      f(avatar);
      f(name);
      f(status);
      f(tiles);
      f(person_tiles);
      f(band_1);
      f(id_text);
    }
  };
  nodes::Memo<view, head> upper;
  nodes::Box<> band_2 = section_band();
  struct members_head : nodes::Stack {
    icon_view people{icon::people{}};
    nodes::Text title;
    icon_button<not_yet<Actions>> add_member;
    members_head(Actions* a, std::size_t count)
        : title(std::format("{} MEMBER{}", count, count == 1 ? "" : "S"), 13.0f, dim_colour, true),
          add_member(icon::add_person{}, {a, "Adding members"}) {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fill = true, .padding = {6.0f, 10.0f, 6.0f, 16.0f}});
      title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(people);
      f(title);
      f(add_member);
    }
  };
  // The members' head, as a function of how many there are.
  nodes::Memo<std::size_t, members_head> members_header;
  // The members, in a list of their own that scrolls, reconciled: its place
  // and its rows kept while they show the same.
  nodes::ScrollContainer<nodes::Flow<std::vector<member_row<open_person>>>> members{
      nodes::Flow<std::vector<member_row<open_person>>>({.spacingY = 0.0f, .wrap = false}, {})};
  // The group's view, to come back to from a member's page.
  view group_view;

  static constexpr float kWidth = 340.0f;

  explicit info_panel(Actions* a) : actions(a) {
    fState.apply({.background = sidebar_colour, .masking = true});
    edge.apply({.place = scene::anchor::kTopLeft, .fillY = true, .width = 1.0f});
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
                        : presence_of(now, one.id.account, one.id.id),
                  group,
                  muted,
                  false};
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

  void forEachChild(auto&& f) {
    f(upper);
    f(band_2);
    f(members_header);
    f(members);
    f(edge);
  }

};

}  // namespace mux::ui
