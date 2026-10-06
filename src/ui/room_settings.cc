// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:room_settings -- A room's settings, as Element's Room Settings
// dialog: its tabs down the left -- General, Security & Privacy, Roles &
// Permissions, Notifications, Advanced -- and the chosen one beside them,
// in Element's words. What the user's power level does not allow is not
// offered: shown as it is, not to be changed.
export module mux.ui:room_settings;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :themes;
import :names;
import :forms;
import :info;

export namespace mux::ui {

// What the dialog shows of a room, as it was when it opened.
struct room_settings_facts {
  std::string id;  // the room's ID
  std::string name;
  std::string topic;
  std::optional<std::string> alias;
  std::vector<std::string> other_aliases;
  bool encrypted = false;
  // Its protocol's part (Matrix: its rules and levels), for its pages.
  room_part_t theirs;
  // Its notifications, as chosen for it (muted: off).
  config::notify_choices notify;
  // Which of its room events it shows, as chosen for it: none chosen is as
  // its account's.
  std::optional<bool> events_all;
  // Whether it shows link previews, as chosen for it.
  std::optional<bool> typing;  // others told one is typing: its own choice
  std::optional<bool> previews;
  std::optional<bool> previews_direct;  // where its previews come from: its own choice
  // Whether it shows who has read up to where, as chosen for it.
  std::optional<bool> receipts;
  // How far a jump's search pages back in it, as chosen for it.
  std::optional<std::int64_t> jump_search;
  std::optional<config::room_event_kinds> event_kinds;
  // The user's own level, as the protocol fills it (manage_facts).
  std::int64_t mine = 0;
  // A room by its ID and name.
  struct named_room {
    std::string id;
    std::string name;
  };
  // The spaces it is in, as theirs list it: what a rule for their members
  // names.
  std::vector<named_room> parents;
  // A space's: the rooms and spaces it holds, and the account's others --
  // those that may be added to it.
  std::vector<named_room> children;
  std::vector<named_room> addable;
  // A space: whether it holds spaces, and whether it is shown as a forum.
  bool space = false;
  bool holds_spaces = false;
  bool forum = false;
  bool hidden_from_home = false;  // a space whose rooms Home leaves out
  // The protocol the room is of: which tabs it has besides the client's own.
  protocol_t speaks;
  // Those whose level is not the default: Element's privileged users.
  struct person {
    std::string id;
    std::string name;
    std::int64_t level = 0;
  };
  std::vector<person> privileged;
};

// Element's names for levels: 100 Admin, 50 Moderator, the default Default.
[[nodiscard]] inline std::string level_name(std::int64_t level, std::int64_t fallback) {
  if (level >= 100)
    return "Admin";
  if (level >= 50)
    return "Moderator";
  if (level == fallback)
    return "Default";
  return std::format("Custom level ({})", level);
}

// Which tabs a room's protocol has: a list of their types, its own -- the
// client's (General, Notifications, Appearance) every room has. A protocol's
// are what manage_tabs(state) lists, found by ADL in its folder (mux.ui.
// proto.<protocol>); each tab type has its title, its icon and its page by
// overloads of its own (tab_title, tab_icon, page_type). Asked where the
// dialog is made -- a template on Actions, made in the program, which
// imports every protocol's. A protocol with none: the default, no tabs.
template <class... Tabs>
struct manage_tab_list {};
namespace manage_defaults {
constexpr manage_tab_list<> manage_tabs(const auto&) { return {}; }
// And what a protocol keeps while its pages are made again: none by default.
struct no_part {};
constexpr type_tag<no_part> manage_part_type(const auto&) { return {}; }
}  // namespace manage_defaults
// Asked of a state type the caller names: dependent on it, so found where
// the dialog is made, not here.
template <class State>
constexpr auto manage_tabs_of(const State& state) {
  using manage_defaults::manage_tabs;
  return manage_tabs(state);
}
template <class State>
constexpr auto manage_part_of(const State& state) {
  using manage_defaults::manage_part_type;
  return manage_part_type(state);
}
template <class... Tabs, class Tab>
[[nodiscard]] constexpr bool lists(manage_tab_list<Tabs...>, type_tag<Tab>) {
  struct all : type_tag<Tabs>... {};
  return std::derived_from<all, type_tag<Tab>>;
}

// The client's tabs, every room's.
namespace settings_tab {
struct general {};        // the room's events, receipts, previews, typing, Home, leaving
struct notifications {};
struct looks {};
}  // namespace settings_tab

template <class Tabs>
struct tab_types;
template <class... Tabs>
struct tab_types<manage_tab_list<Tabs...>> {
  using type = type_list<Tabs...>;
};
template <class Rule, class Variant>
[[nodiscard]] bool is_rule(const Variant& now) {
  return spl::visit(spl::overloaded{[](const Rule&) { return true; }, [](const auto&) { return false; }}, now);
}

// A heading over a tab, and over a part of one, as Element's.
inline nodes::Text tab_heading(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 20.0f, colours.text, true);
  out.apply({.margin = {0.0f, 0.0f, 12.0f, 0.0f}});
  return out;
}
inline nodes::Text part_heading(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 15.0f, colours.text, true);
  out.apply({.margin = {18.0f, 0.0f, 4.0f, 0.0f}});
  return out;
}
inline nodes::Text explained(const palette& colours, std::string text) {
  nodes::Text out(std::move(text), 13.0f, colours.dim);
  out.setWrapped(true);
  out.apply({.fillX = true, .margin = {2.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// One of a choice, as Element's radio buttons: a ring, and a title over
// what it means.
template <class Act>
struct radio_choice : pressable<nodes::Stack> {
  Act act;
  struct texts : nodes::Stack {
    struct parts_t {
      nodes::Text title;
      nodes::Text about;
    } parts;
    texts(const palette& colours, std::string title, std::string about)
        : parts{.title = nodes::Text(std::move(title), 14.0f, colours.text),
                .about = nodes::Text(std::move(about), 12.0f, colours.dim)} {
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.title.setWrapped(true);
      parts.title.apply({.fillX = true});
      parts.about.setWrapped(true);
      parts.about.apply({.fillX = true});
      parts.about.setVisible(!parts.about.text().empty());
    }
  };
  struct parts_t {
    radio_mark ring;
    texts words;
  } parts;
  radio_choice(const palette& colours, std::string title, std::string about, Act what, bool on, bool allowed)
      : act(std::move(what)), parts{.ring = radio_mark(colours), .words = texts(colours, std::move(title), std::move(about))} {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 6.0f, 6.0f, 6.0f},
                  .cornerRadius = 6.0f, .hoverBackground = colours.chosen, .disabled = !allowed});
    if (allowed)
      fState.setCursor(scene::cursor::hand{});
    parts.ring.set_on(on);
    if (!allowed)
      fState.setAlpha(0.55f);
  }
};

// A switch with what it does beside it, as Element's labelled toggles.
template <class Act>
struct toggle_line : nodes::Stack {
  struct parts_t {
    nodes::Text label;
    widgets::Toggle<Act> toggle;
  } parts;
  toggle_line(const palette& colours, std::string text, Act what, bool on, bool allowed)
      : parts{.label = nodes::Text(std::move(text), 14.0f, colours.text), .toggle = widgets::Toggle<Act>(colours.widgets, std::move(what))} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}, .disabled = !allowed});
    parts.label.setWrapped(true);
    parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    parts.toggle.apply({.alignSelf = scene::align::kMiddle});
    parts.toggle.setOnNow(on);
    if (!allowed)
      fState.setAlpha(0.55f);
  }
};

// Leaving a space, as Element's LeaveSpaceDialog: the rooms of it one is in
// left with it -- none of them, all, or those chosen, each by a switch.
struct leave_space_facts {
  conversation_id space;
  std::string name;
  std::vector<room_settings_facts::named_room> rooms;  // its rooms one is in
};
namespace leave_choice {
struct none {};
struct all {};
struct some {};
}  // namespace leave_choice
using leave_choice_t = spl::variant<leave_choice::none, leave_choice::all, leave_choice::some>;
template <class Actions>
struct leave_space_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fitting{440.0f}}; }
  Actions* actions = nullptr;
  leave_space_facts facts;
  leave_choice_t choice = leave_choice::none{};
  std::set<std::string> chosen;  // the rooms to leave, where some are
  struct pick {
    leave_space_box* box;
    leave_choice_t to;
    void operator()() const {
      box->choice = to;
      box->show();
    }
  };
  struct flip_room {
    leave_space_box* box;
    std::string room;
    void operator()() const {
      if (!box->chosen.erase(room))
        box->chosen.insert(room);
    }
  };
  struct go {
    leave_space_box* box;
    void operator()() const { box->actions->leave_space(box->facts.space, box->leaving()); }
  };
  struct cancel {
    Actions* actions;
    void operator()() const { actions->close_leave_space(); }
  };
  struct parts_t {
    nodes::Text title;
    nodes::Text about;
    radio_choice<pick> none, all, some;
    std::vector<toggle_line<flip_room>> rooms;
    dialog_buttons<cancel, go> buttons;
  } parts;
  leave_space_box(const ui_needs<Actions>& n, leave_space_facts what)
      : actions(n.actions), facts(std::move(what)),
        parts{.title = nodes::Text("Leave " + facts.name, 17.0f, n.colours->text, true),
              .about = explained(*n.colours, facts.rooms.empty()
                                                 ? "You are in none of its rooms."
                                                 : "Would you like to leave the rooms in this space too?"),
              .none = radio_choice<pick>(*n.colours, "Don't leave any rooms", "", {this, leave_choice::none{}}, true, true),
              .all = radio_choice<pick>(*n.colours, "Leave all rooms", "", {this, leave_choice::all{}}, false, true),
              .some = radio_choice<pick>(*n.colours, "Leave some rooms", "", {this, leave_choice::some{}}, false, true),
              .buttons = dialog_buttons<cancel, go>(*n.colours, "Leave space", {n.actions}, {this}, 130.0f)} {
    for (const auto& one : facts.rooms)
      parts.rooms.emplace_back(*n.colours, one.name, flip_room{this, one.id}, false, true);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
    this->setGap(8.0f);
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.none, &parts.all, &parts.some})
      each->setVisible(!facts.rooms.empty());
    this->show();
  }
  // The choice made shown: its ring lit, the rooms' switches where some.
  void show() {
    const auto is = [&](auto which) {
      return spl::visit(spl::overloaded{[](decltype(which)) { return true; }, [](const auto&) { return false; }}, choice);
    };
    parts.none.parts.ring.set_on(is(leave_choice::none{}));
    parts.all.parts.ring.set_on(is(leave_choice::all{}));
    parts.some.parts.ring.set_on(is(leave_choice::some{}));
    for (auto& one : parts.rooms)
      one.setVisible(is(leave_choice::some{}));
    this->invalidateLayout();
    this->markDamaged();
  }
  // The rooms to leave with it, as chosen.
  [[nodiscard]] std::vector<std::string> leaving() const {
    return spl::visit(
        spl::overloaded{[](const leave_choice::none&) { return std::vector<std::string>{}; },
                        [&](const leave_choice::all&) {
                          return std::ranges::to<std::vector<std::string>>(
                              std::views::transform(facts.rooms, [](const room_settings_facts::named_room& one) { return one.id; }));
                        },
                        [&](const leave_choice::some&) { return std::vector<std::string>(chosen.begin(), chosen.end()); }},
        choice);
  }
};

// A text to copy, as Element's "Internal room ID": the text, and a button.
struct copy_line : nodes::Stack {
  struct copy_it {
    std::string text;
    void operator()() const { skiff::scene::setClipboardText(text); }
  };
  struct parts_t {
    nodes::Text label;
    nodes::Text value;
    widgets::Button<copy_it> copy;
  } parts;
  copy_line(const palette& colours, std::string label, std::string value)
      : parts{.label = nodes::Text(std::move(label), 14.0f, colours.dim),
              .value = nodes::Text(value, 14.0f, colours.text),
              .copy = widgets::Button<copy_it>(colours.widgets, "Copy", {value})} {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 4.0f, 0.0f}});
    parts.label.apply({.alignSelf = scene::align::kMiddle});
    parts.value.setElided(true);
    parts.value.apply({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    parts.copy.apply({.width = 70.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle});
  }
};

template <class Actions>
struct room_settings : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{860.0f, 620.0f}}; }
  using actions_type = Actions;
  Actions* actions = nullptr;
  // The colours it and its pages are made in: what it was handed.
  const palette* colours_ = nullptr;
  // The looks shown: the program's.
  const looks_shown* looks_ = nullptr;
  room_settings_facts facts;

  // ---- the tabs and pages: the client's, then each protocol's -----------------
  template <class Tag>
  using tabs_of_t = decltype(manage_tabs_of(state_of<Tag>{}));
  template <class Tab>
  using page_of_t = typename decltype(page_type(Tab{}, type_tag<room_settings>{}))::type;
  template <class List>
  struct pages_of;
  template <class... Tabs>
  struct pages_of<type_list<Tabs...>> {
    using type = type_list<page_of_t<Tabs>...>;
  };
  template <class>
  struct protocol_lists;
  template <class... Tags>
  struct protocol_lists<protocol_list<Tags...>> {
    using tabs = typename joined<type_list<>, typename tab_types<tabs_of_t<Tags>>::type...>::type;
    using parts = std::tuple<typename decltype(manage_part_of(state_of<Tags>{}))::type...>;
  };
  using protocol_tabs = typename protocol_lists<protocols>::tabs;
  using protocol_pages = typename pages_of<protocol_tabs>::type;
  using settings_tab_t = typename variant_of_types<typename joined<
      type_list<settings_tab::general, settings_tab::notifications, settings_tab::looks>, protocol_tabs>::type>::type;

  // ---- what is asked ---------------------------------------------------------
  struct pick_tab {
    room_settings* box;
    settings_tab_t tab;
    void operator()() const {
      box->to_top = true;
      box->show_tab(tab);
    }
  };

  // ---- the tabs down the left ------------------------------------------------
  struct tab_row : pressable<nodes::Stack> {
    pick_tab act;
    struct parts_t {
      icon_mark mark;
      nodes::Text label;
    } parts;
    tab_row(const palette& colours, std::string text, icon_t icon, pick_tab what)
        : act(std::move(what)),
          parts{.mark = icon_mark(colours, icon), .label = nodes::Text(std::move(text), 14.0f, colours.text)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = colours.chosen, .selectedBackground = colours.chosen});
      parts.mark.apply({.alignSelf = scene::align::kMiddle});
      parts.label.setElided(true);
      parts.label.apply({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    void set_chosen(bool on) { fState.apply({.selected = on}); }
  };
  struct tab_list : nodes::Stack {
    struct parts_t {
      tab_row general;
      std::vector<tab_row> protocol;  // the room's protocol's tabs, as it lists them
      tab_row notifications, looks;
    } parts;
    template <class... Tabs>
    void add(room_settings* box, manage_tab_list<Tabs...>) {
      (parts.protocol.emplace_back(*box->colours_, std::string(tab_title(Tabs{})), tab_icon(Tabs{}), pick_tab{box, settings_tab_t{Tabs{}}}), ...);
    }
    explicit tab_list(room_settings* box)
        : parts{.general = tab_row(*box->colours_, "General", icon::gear{}, {box, settings_tab::general{}}),
                .notifications = tab_row(*box->colours_, "Notifications", icon::bell{}, {box, settings_tab::notifications{}}),
                .looks = tab_row(*box->colours_, "Appearance", icon::eye{}, {box, settings_tab::looks{}})} {
      spl::visit([&](auto of) { this->add(box, tabs_of_t<decltype(of)>{}); }, box->facts.speaks);
      this->setGap(2.0f);
      fState.apply({.fillY = true, .width = 220.0f, .padding = {4.0f, 12.0f, 12.0f, 12.0f}});
    }
    [[nodiscard]] std::vector<tab_row*> rows() {
      std::vector<tab_row*> out{&parts.general};
      for (tab_row& one : parts.protocol)
        out.push_back(&one);
      out.push_back(&parts.notifications);
      out.push_back(&parts.looks);
      return out;
    }
    void show(const settings_tab_t& tab) {
      for (tab_row* one : this->rows())
        one->set_chosen(one->act.tab.index() == tab.index());
    }
  };

  // ---- General: the room as the client shows it -------------------------------------
  // A space as one chat, its rooms as topics: a switch, off for a space
  // that holds spaces.
  struct flip_forum_act {
    Actions* actions;
    std::string room;
    bool allowed = true;
    void operator()() const {
      if (allowed)
        actions->flip_forum(room);
    }
  };
  // A space's rooms out of Home, or in it: a switch, for a space that is
  // not shown as one chat.
  struct flip_home_hide_act {
    Actions* actions;
    std::string room;
    void operator()() const { actions->flip_home_hide(room); }
  };
  struct general_page : nodes::Stack {
    struct parts_t {
      nodes::Text heading;
      nodes::Text events_about;
      chat_choices<Actions> chats;
      typing_choice<Actions> typing;
      nodes::Text forum_heading;
      toggle_line<flip_forum_act> forum;
      nodes::Text forum_about;
      toggle_line<flip_home_hide_act> home_hide;
      nodes::Text leave_heading;
      widgets::Button<ask<Actions, &Actions::leave_chat>> leave;
    } parts;
    general_page(Actions* a, room_settings* box, const room_settings_facts& facts)
        : parts{.heading = tab_heading(*box->colours_, "General"),
                .events_about = explained(*box->colours_, "Room events shown in this room, for you: Default is as your account's."),
                .chats = chat_choices<Actions>(a, *box->colours_, choice_level::chat{},
                                               {.events_all = facts.events_all,
                                                .event_kinds = facts.event_kinds,
                                                .receipts = facts.receipts,
                                                .previews = facts.previews,
                                                .previews_direct = facts.previews_direct,
                                                .jump_search = facts.jump_search},
                                               6.0f),
                .typing = typing_choice<Actions>(a, *box->colours_, choice_level::chat{}, facts.typing),
                .forum_heading = part_heading(*box->colours_, "Shown as"),
                .forum = toggle_line<flip_forum_act>(*box->colours_, "One chat, its rooms as topics", {a, facts.id, !facts.holds_spaces},
                                                     facts.forum, !facts.holds_spaces),
                .forum_about = explained(*box->colours_, facts.holds_spaces
                                             ? "A space that holds spaces is shown as a space."
                                             : "On: in the chat list as one chat; its rooms open inside it, as Telegram's topics."),
                .home_hide = toggle_line<flip_home_hide_act>(*box->colours_, "Its rooms not in Home", {a, facts.id}, facts.hidden_from_home, true),
                .leave_heading = part_heading(*box->colours_, "Leave room"),
                .leave = widgets::Button<ask<Actions, &Actions::leave_chat>>(box->colours_->widgets, "Leave room", {a})} {
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.forum_heading, &parts.forum, &parts.forum_about})
        each->setVisible(facts.space);
      // A space's own: its rooms out of Home -- not one shown as one chat,
      // whose rooms are in it, not in the list.
      parts.home_hide.setVisible(facts.space && !facts.forum);
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
      parts.leave.apply({.width = 130.0f, .height = 32.0f});
    }
  };

  // ---- Notifications ----------------------------------------------------------------
  struct notifications_page : nodes::Stack {
    struct parts_t {
      nodes::Text heading;
      notify_choice_rows<Actions> choices;
      nodes::Text note;
    } parts;
    notifications_page(Actions* a, room_settings* box, const room_settings_facts& facts)
        : parts{.heading = tab_heading(*box->colours_, "Notifications"),
                .choices = notify_choice_rows<Actions>(a, *box->colours_, choice_level::chat{}, facts.notify),
                .note = nodes::Text(facts.space ? "For every chat in this space, unless the chat chooses again; Default is "
                                                  "as the space above it, or the account, says."
                                                : "Default is as the space it is in, or the account, says.",
                                    12.0f, box->colours_->dim)} {
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
      parts.note.setWrapped(true);
      parts.note.apply({.fillX = true});
    }
  };

  // ---- Appearance: the room's background, bubbles and panels --------------------
  struct looks_page : nodes::Stack {
    struct parts_t {
      nodes::Text heading;
      look_choices<Actions> choices;
    } parts;
    looks_page(Actions* a, room_settings* box, const room_settings_facts&)
        : parts{.heading = tab_heading(*box->colours_, "Appearance"), .choices = look_choices<Actions>(a, *box->colours_, *box->looks_, choice_level::chat{})} {
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    }
  };

  using page_t = typename variant_of_types<
      typename joined<type_list<general_page, notifications_page, looks_page>, protocol_pages>::type>::type;
  struct page_holder : nodes::Stack {
    struct parts_t {
      page_t page;
    } parts;
    explicit page_holder(page_t first) : parts{.page = std::move(first)} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    }
  };

  // ---- the dialog ---------------------------------------------------------------------
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_manage(); }
  };
  using header_t = page_header<no_back, close_it>;
  struct body_row : nodes::Stack {
    struct parts_t {
      tab_list tabs;
      nodes::ScrollContainer<page_holder> content;
    } parts;
    body_row(room_settings* box, page_t first)
        : parts{.tabs = tab_list(box), .content = nodes::ScrollContainer<page_holder>(page_holder(std::move(first)))} {
      this->setHorizontal();
      fState.apply({.fillX = true, .grow = scene::axes::kY});
      parts.content.apply({.fillY = true, .grow = scene::axes::kX});
    }
    // Narrow -- a phone's -- the tabs a thin column of their icons beside the
    // page, the page the rest of the width: beside a column of 220 it had a
    // hundred. Their sizing as it is, only narrower -- flipped from a column
    // to a row and back, the sizes clashed and the page was left empty.
    bool narrow = false;
    void layoutChildren() {
      if (const bool now = fState.contentBox().width() < 560.0f; now != narrow) {
        narrow = now;
        auto& [tabs, content] = parts;
        tabs.apply({.width = narrow ? 60.0f : 220.0f,
                    .padding = narrow ? scene::Margin{4.0f, 6.0f, 12.0f, 6.0f} : scene::Margin{4.0f, 12.0f, 12.0f, 12.0f}});
        // Narrow, the rows' sides 4, not 12: their icon (28) had 24 of 48.
        for (tab_row* one : tabs.rows()) {
          one->parts.label.setVisible(!narrow);
          one->apply({.padding = narrow ? scene::Margin{0.0f, 4.0f, 0.0f, 4.0f} : scene::Margin{0.0f, 12.0f, 0.0f, 12.0f}});
        }
        tabs.invalidateLayout();
        content.invalidateLayout();
        this->invalidateLayout();
      }
      this->nodes::Stack::layoutChildren();
    }
  };
  struct parts_t {
    header_t header;
    body_row body;
  } parts;

  settings_tab_t tab = settings_tab::general{};
  // What each protocol keeps while its pages are made again -- Matrix's: an
  // encryption being confirmed, the level being picked -- in the list's order.
  typename protocol_lists<protocols>::parts protocol_parts;
  // A tab to be made again, at the next frame: not from inside a press on
  // what it replaces.
  bool rebuild_due = false;
  bool to_top = false;  // another tab: shown from its top

  room_settings(const ui_needs<Actions>& n, const room_settings_facts& shown) : room_settings(n.colours, n.looks, n.actions, shown) {}
  room_settings(const palette* colours, const looks_shown* looks, Actions* a, const room_settings_facts& shown)
      : actions(a), colours_(colours), looks_(looks), facts(shown),
        parts{.header = header_t(*colours, "Room Settings - " + shown.name, {}, {a}, false, true),
              .body = body_row(this, page_t(std::in_place_index<0>, a, this, shown))} {
    fState.apply({.fill = true});
    parts.body.parts.tabs.show(tab);
  }

  [[nodiscard]] page_holder& holder() { return std::get<0>(parts.body.parts.content.fChildren); }
  // What a protocol keeps here: by its state type.
  template <class State>
  [[nodiscard]] auto& part() {
    return std::get<index_in<proto::id<State>>(protocols{})>(protocol_parts);
  }
  template <class Tag, class... Tags>
  static constexpr std::size_t index_in(protocol_list<Tags...>) {
    constexpr std::array<bool, sizeof...(Tags)> is{std::derived_from<Tag, Tags>...};
    return static_cast<std::size_t>(std::ranges::find(is, true) - is.begin());
  }
  // A tab shown, made from the facts as they now are -- at the next frame.
  void show_tab(const settings_tab_t& to) {
    tab = to;
    rebuild_due = true;
    this->markDamaged();
  }
  // The tab up, made again: what a page's act asks, once it changed the facts.
  void show_again() { this->show_tab(tab); }
  [[nodiscard]] bool settling() const { return rebuild_due; }
  void update(double) {
    if (std::exchange(rebuild_due, false))
      this->rebuild();
  }
  void rebuild() {
    const settings_tab_t to = tab;
    auto& page = holder().parts.page;
    spl::visit(spl::overloaded{
                      [&](settings_tab::general) { page.template emplace<general_page>(actions, this, facts); },
                      [&](settings_tab::notifications) { page.template emplace<notifications_page>(actions, this, facts); },
                      [&](settings_tab::looks) { page.template emplace<looks_page>(actions, this, facts); },
                      // A protocol's tab: the page its page_type() gives.
                      [&](auto theirs) { page.template emplace<page_of_t<decltype(theirs)>>(actions, this, facts); }},
                  to);
    parts.body.parts.tabs.show(tab);
    if (std::exchange(to_top, false))
      parts.body.parts.content.scrollTo(0.0f);
    this->invalidateLayout();
  }
};

}  // namespace mux::ui
