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

export import :info_common;
export import :info_cards;
export import :info_reactions;
export import :info_new_chats;
export import :info_looks;
export import :info_threads;
export import :info_packs;
export import :info_explore;

export namespace mux::ui {

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
  // The colours it is made in, for what it makes later; and what the
  // window's parts share: the accounts' protocol states.
  const palette* colours_ = nullptr;
  const ui_shared* shared_ = nullptr;
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
  std::uint64_t trust_seen = 0;
  // Only the first of them made, and more as the list is scrolled near its
  // end, as Qt's fetchMore: a big room has thousands, each with its role,
  // badges and presence worked out, and all of them were made at every
  // switch to it. How many there are, and how many were made.
  static constexpr std::size_t kMembersStep = 60;
  std::size_t members_made = kMembersStep;
  std::size_t members_built = 0;
  std::size_t members_total = 0;
  // The member open on their page, where they are past those made.
  std::optional<std::pair<member, std::string>> person_entry;
  // To be shown again at the next frame: a member opened past those made.
  bool wants_show = false;
  // Near the end of the members made, with more there: the next few wanted.
  [[nodiscard]] bool wants_more() {
    return members.visible() && members_built < members_total &&
           parts.scroll.atEnd(std::max(300.0f, parts.scroll.bounds().height() * 1.5f));
  }

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
    // Whether its protocol lets it be left: its Leave tile only then.
    bool leavable = true;
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
          : parts{.back = icon_button<back_to_group>(*panel->colours_, icon::back{}, {panel}),
                  .close = close_button(*panel->colours_, icon::close{}, {a})} {
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
      tiles_row(Actions* a, const palette& colours, bool muted, bool leavable)
          : parts{.mute = mute_tile(colours, muted ? "Unmute" : "Mute", icon::bell{}, {a}),
                  .manage = manage_tile(colours, "Manage", icon::sliders{}, {a}),
                  .leave = leave_tile(colours, "Leave", icon::leave{}, {a})} {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        parts.mute.apply({.grow = scene::axes::kX});
        parts.manage.apply({.grow = scene::axes::kX});
        parts.leave.apply({.grow = scene::axes::kX});
        parts.leave.setVisible(leavable);
      }
    };
    // A member's own: a message to them.
    struct person_row : nodes::Stack {
      struct parts_t {
        action_tile<message_them> message;
      } parts;
      person_row(Actions* a, info_panel* panel)
          : parts{.message = action_tile<message_them>(*panel->colours_, "Message", icon::send{}, {a, panel})} {
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
        nodes::Text label;
      } parts;
      about_block(const palette& colours, const std::string& said)
          : parts{.text = nodes::Text(said, 14.0f, colours.text), .label = nodes::Text("Description", 12.0f, colours.dim)} {
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}});
        parts.text.setWrapped(true);
        parts.text.setSelectable(true);
        parts.text.setLinks(link_spans_in(said), colours.accent);
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
      nodes::Box<> band_1;
      about_block about;
      std::vector<id_line> addresses;
      id_line id_text;
    } parts;

    head(Actions* a, info_panel* panel, const view& shown)
        : parts{.top = top_row(a, panel, shown.of_person),
                .avatar = avatar_button<Actions>(a, shown.key, shown.name, 96.0f),
                .name = nodes::Text(shown.name, 17.0f, panel->colours_->text, true),
                .status = nodes::Text(shown.status, 13.0f, panel->colours_->dim),
                .band_1 = section_band(*panel->colours_),
                .about = about_block(*panel->colours_, shown.topic),
                .id_text = id_line(*panel->colours_, shown.key, shown.copied)} {
      auto& [top, avatar, name, status, tiles, person_tiles, band_1, about, addresses, id_text] = parts;
      about.setVisible(!shown.topic.empty());
      for (const std::string& address : shown.addresses)
        addresses.emplace_back(*panel->colours_, address, "", "Address");
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      if (shown.of_person)
        person_tiles.emplace(a, panel);
      else
        tiles.emplace(a, *panel->colours_, shown.muted, shown.leavable);
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
      icon_view people;
      nodes::Text title;
      add_button add_member;
    } parts;
    members_head(Actions* a, const palette& colours, std::size_t count)
        : parts{.people = icon_view(colours, icon::people{}),
                .title = nodes::Text(std::format("{} MEMBER{}", count, count == 1 ? "" : "S"), 13.0f, colours.dim, true),
                .add_member = add_button(colours, icon::add_person{}, {a, "Adding members"})} {
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
      nodes::Box<> band_2;
      // The members' head, as a function of how many there are.
      nodes::Memo<std::size_t, members_head> members_header;
      // The members, reconciled: the rows kept while they show the same.
      member_rows members{{.spacingY = 0.0f, .wrap = false}, {}};
    } parts;
    explicit column(const palette& colours) : parts{.band_2 = section_band(colours)} {
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };
  struct parts_t {
    nodes::ScrollContainer<column> scroll;
    nodes::Box<> edge;  // its left edge
  } parts;
  column& content = std::get<0>(parts.scroll.fChildren);
  nodes::Memo<view, head>& upper = content.parts.upper;
  nodes::Box<>& band_2 = content.parts.band_2;
  nodes::Memo<std::size_t, members_head>& members_header = content.parts.members_header;
  member_rows& members = content.parts.members;
  // The group's view, to come back to from a member's page.
  view group_view;

  static constexpr float kWidth = 340.0f;

  info_panel(Actions* a, const palette& colours, const ui_shared& shared) : actions(a), colours_(&colours), shared_(&shared), parts{.scroll = nodes::ScrollContainer<column>(column(colours)), .edge = nodes::Box<>(colours.band)} {
    fState.apply({.background = colours.sidebar, .masking = true});
    parts.edge.apply({.place = scene::anchor::kTopLeft, .fillY = true, .width = 1.0f});
    parts.scroll.apply({.fillX = true, .grow = scene::axes::kY});
    upper.apply({.fillX = true, .autoSize = scene::axes::kY});
    members_header.apply({.fillX = true, .height = 48.0f});
    members.apply({.fillX = true, .autoSize = scene::axes::kY});
  }

  // A member as the list shows them: their role, as the chat's protocol
  // says it (Matrix: its power levels, as Element marks its admins and
  // moderators); and what their protocol says of them beside how they are
  // (Matrix: in an encrypted room, their identity -- Element's shield).
  [[nodiscard]] std::pair<member, std::string> entry_of(const conversation& one, const model& now, const member& each) const {
    member shown = each;
    if (!shown.role)
      if (std::string role = proto::sender_role(protocol_state_of(*shared_, one.id.account), one, each.id); !role.empty())
        shown.role = std::move(role);
    std::string how = std::ranges::fold_left(
        proto::person_badges(protocol_state_of(*shared_, one.id.account), &one, now, one.id.account, each.id),
        presence_of(*shared_, now, one.id.account, each.id), [](std::string so_far, const proto::part::badge& badge) {
          return so_far.empty() ? badge.text : std::format("{} \u00b7 {}", so_far, badge.text);
        });
    return {std::move(shown), std::move(how)};
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
                        : presence_of(*shared_, now, one.id.account, contact_of(*shared_, one)),
                  group,
                  muted,
                  false};
    if (group)
      if (auto link = proto::room_link(protocol_state_of(*shared_, one.id.account), one))
        group_view.copied = std::move(*link);
    group_view.topic = one.topic.value_or("");
    group_view.leavable = proto::can_leave(protocol_state_of(*shared_, one.id.account), one);
    if (one.alias)
      group_view.addresses.push_back(*one.alias);
    std::ranges::copy(one.other_aliases, std::back_inserter(group_view.addresses));
    // Its members made again only where they changed -- or another chat's
    // are shown: a big room has thousands, and every refresh rebuilt them.
    if (members_of != one.id)
      members_made = kMembersStep;
    const std::size_t making = std::min(members_made, one.members.size());
    const bool same_members = members_of == one.id && members_revision == one.members_revision &&
                              trust_seen == now.trust_revision() && members_built == making;
    members_of = one.id;
    members_revision = one.members_revision;
    trust_seen = now.trust_revision();
    members_total = one.members.size();
    if (!same_members) {
      shown_members.clear();
      shown_members.reserve(making);
      for (const member& each : std::views::take(one.members, making))
        shown_members.push_back(this->entry_of(one, now, each));
      members_built = making;
    }
    // The member open, where they are past those made: theirs alone.
    person_entry.reset();
    if (person && !std::ranges::contains(shown_members, *person, [](const auto& each) { return each.first.id; }))
      if (const auto found = std::ranges::find(one.members, *person, &member::id); found != one.members.end())
        person_entry = this->entry_of(one, now, *found);
    auto& rows = std::get<0>(members.fChildren);
    if (!same_members && nodes::reconcile(
            rows, shown_members, [](const auto& each) { return each.first.id; },
            [](const member_row<open_person>& row) { return row.id; },
            [&](const auto& each) { return member_row<open_person>(*colours_, each.first, each.second, open_person{this}); },
            [](const member_row<open_person>& row, const auto& each) {
              return row.who == each.first && row.how_shown == each.second;
            }))
      members.invalidateLayout();
    members_header.show(static_cast<std::size_t>(std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count)),
                        [this](std::size_t count) { return members_head(actions, *colours_, count); });
    this->render();
  }
  void open_member(std::string id) {
    person = std::move(id);
    // Past those made: theirs worked out at the next frame.
    wants_show = !std::ranges::contains(shown_members, *person, [](const auto& each) { return each.first.id; });
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
      const auto made = std::ranges::find(shown_members, *person, [](const auto& each) { return each.first.id; });
      const std::pair<member, std::string>* found =
          made != shown_members.end() ? &*made : (person_entry && person_entry->first.id == *person ? &*person_entry : nullptr);
      if (found) {
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
