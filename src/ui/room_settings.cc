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
  join_rule_t join_rule = join_rule::invite{};
  history_rule_t history = history_rule::shared{};
  std::string version;
  config::notify_mode_t notify_mode = config::notify_mode::by_default{};
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
  // The user's own level, and what each thing done asks.
  std::int64_t mine = 0;
  power_needs needs;
  // A space: whether it holds spaces, and whether it is shown as a forum.
  bool space = false;
  bool holds_spaces = false;
  bool forum = false;
  bool hidden_from_home = false;  // a space whose rooms Home leaves out
  // Those whose level is not the default: Element's privileged users.
  struct person {
    std::string id;
    std::string name;
    std::int64_t level = 0;
  };
  std::vector<person> privileged;
  [[nodiscard]] bool may(const power_need_t& need) const { return mine >= needs.of(need); }
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

// The tabs, as Element lists them.
namespace settings_tab {
struct general {};
struct security {};
struct roles {};
struct notifications {};
struct advanced {};
struct looks {};
}  // namespace settings_tab
using settings_tab_t = splice::variant<settings_tab::general, settings_tab::security, settings_tab::roles,
                                    settings_tab::notifications, settings_tab::advanced, settings_tab::looks>;

// A heading over a tab, and over a part of one, as Element's.
inline nodes::Text tab_heading(std::string text) {
  nodes::Text out(std::move(text), 20.0f, text_colour, true);
  out.apply({.margin = {0.0f, 0.0f, 12.0f, 0.0f}});
  return out;
}
inline nodes::Text part_heading(std::string text) {
  nodes::Text out(std::move(text), 15.0f, text_colour, true);
  out.apply({.margin = {18.0f, 0.0f, 4.0f, 0.0f}});
  return out;
}
inline nodes::Text explained(std::string text) {
  nodes::Text out(std::move(text), 13.0f, dim_colour);
  out.setWrapped(true);
  out.apply({.fillX = true, .margin = {2.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// One of a choice, as Element's radio buttons: a ring, and a title over
// what it means.
template <class Act>
struct radio_choice : nodes::Stack {
  Act act;
  struct texts : nodes::Stack {
    struct parts_t {
      nodes::Text title;
      nodes::Text about;
    } parts;
    texts(std::string title, std::string about)
        : parts{.title = nodes::Text(std::move(title), 14.0f, text_colour),
                .about = nodes::Text(std::move(about), 12.0f, dim_colour)} {
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
  radio_choice(std::string title, std::string about, Act what, bool on, bool allowed)
      : act(std::move(what)), parts{.words = texts(std::move(title), std::move(about))} {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 6.0f, 6.0f, 6.0f},
                  .cornerRadius = 6.0f, .hoverBackground = chosen_colour, .disabled = !allowed});
    if (allowed)
      fState.setCursor(scene::cursor::hand{});
    parts.ring.set_on(on);
    if (!allowed)
      fState.setAlpha(0.55f);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
};

// A switch with what it does beside it, as Element's labelled toggles.
template <class Act>
struct toggle_line : nodes::Stack {
  struct parts_t {
    nodes::Text label;
    widgets::Toggle<Act> toggle;
  } parts;
  toggle_line(std::string text, Act what, bool on, bool allowed)
      : parts{.label = nodes::Text(std::move(text), 14.0f, text_colour), .toggle = widgets::Toggle<Act>(std::move(what))} {
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
  copy_line(std::string label, std::string value)
      : parts{.label = nodes::Text(std::move(label), 14.0f, dim_colour),
              .value = nodes::Text(value, 14.0f, text_colour),
              .copy = widgets::Button<copy_it>("Copy", {value})} {
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
  Actions* actions = nullptr;
  room_settings_facts facts;

  // ---- what is asked ---------------------------------------------------------
  struct act_on_room {
    Actions* actions;
    room_action_t action;
    void operator()() const { actions->room_act(action); }
  };
  struct pick_tab {
    room_settings* box;
    settings_tab_t tab;
    void operator()() const {
      box->to_top = true;
      box->show_tab(tab);
    }
  };
  struct save_general {
    room_settings* box;
    void operator()() const { box->store_general(); }
  };
  struct cancel_general {
    room_settings* box;
    void operator()() const { box->show_tab(settings_tab::general{}); }
  };
  struct choose_join {
    room_settings* box;
    join_rule_t rule;
    void operator()() const { box->chose(rule); }
  };
  struct choose_history {
    room_settings* box;
    history_rule_t rule;
    void operator()() const { box->chose(rule); }
  };
  struct turn_encryption_on {
    room_settings* box;
    void operator()() const { box->encrypt(); }
  };
  struct set_need {
    room_settings* box;
    power_need_t need;
    std::int64_t level;
    void operator()() const { box->needs_level(need, level); }
  };
  struct set_level {
    room_settings* box;
    std::string user;
    std::int64_t level;
    void operator()() const { box->user_level(user, level); }
  };
  struct pick_new_level {
    room_settings* box;
    std::int64_t level;
    void operator()() const { box->new_level = level; box->show_new_level(); }
  };
  struct add_privileged {
    room_settings* box;
    void operator()() const { box->apply_new_level(); }
  };
  struct set_event_level {
    room_settings* box;
    std::string event;
    std::int64_t level;
    void operator()() const { box->event_level(event, level); }
  };
  struct add_event_need {
    room_settings* box;
    void operator()() const { box->apply_event_level(); }
  };
  struct upgrade_press {
    room_settings* box;
    void operator()() const { box->upgrade(); }
  };
  struct notify_as {
    room_settings* box;
    config::notify_mode_t mode;
    void operator()() const { box->notify(mode); }
  };

  // ---- the tabs down the left ------------------------------------------------
  struct tab_row : nodes::Stack {
    pick_tab act;
    struct parts_t {
      icon_mark mark;
      nodes::Text label;
    } parts;
    tab_row(std::string text, icon_t icon, pick_tab what)
        : act(std::move(what)), parts{.mark = icon_mark(icon), .label = nodes::Text(std::move(text), 14.0f, text_colour)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = chosen_colour, .selectedBackground = chosen_colour});
      parts.mark.apply({.alignSelf = scene::align::kMiddle});
      parts.label.setElided(true);
      parts.label.apply({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    void set_chosen(bool on) { fState.apply({.selected = on}); }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      act();
      return true;
    }
  };
  struct tab_list : nodes::Stack {
    struct parts_t {
      tab_row general, security, roles, notifications, looks, advanced;
    } parts;
    explicit tab_list(room_settings* box)
        : parts{.general = tab_row("General", icon::gear{}, {box, settings_tab::general{}}),
                .security = tab_row("Security & Privacy", icon::eye{}, {box, settings_tab::security{}}),
                .roles = tab_row("Roles & Permissions", icon::people{}, {box, settings_tab::roles{}}),
                .notifications = tab_row("Notifications", icon::bell{}, {box, settings_tab::notifications{}}),
                .looks = tab_row("Appearance", icon::eye{}, {box, settings_tab::looks{}}),
                .advanced = tab_row("Advanced", icon::sliders{}, {box, settings_tab::advanced{}})} {
      this->setGap(2.0f);
      fState.apply({.fillY = true, .width = 220.0f, .padding = {4.0f, 12.0f, 12.0f, 12.0f}});
    }
    void show(const settings_tab_t& tab) {
      const auto is = [&](auto kind) {
        return splice::visit(splice::overloaded{[](decltype(kind)) { return true; }, [](const auto&) { return false; }}, tab);
      };
      parts.general.set_chosen(is(settings_tab::general{}));
      parts.security.set_chosen(is(settings_tab::security{}));
      parts.roles.set_chosen(is(settings_tab::roles{}));
      parts.notifications.set_chosen(is(settings_tab::notifications{}));
      parts.advanced.set_chosen(is(settings_tab::advanced{}));
      parts.looks.set_chosen(is(settings_tab::looks{}));
    }
  };

  // ---- General -------------------------------------------------------------------
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
  struct forum_row : nodes::Stack {
    struct parts_t {
      nodes::Text label{"One chat, its rooms as topics", 14.0f, text_colour};
      widgets::Toggle<flip_forum_act> toggle;
    } parts;
    forum_row(Actions* a, const room_settings_facts& facts)
        : parts{.toggle = widgets::Toggle<flip_forum_act>({a, facts.id, !facts.holds_spaces})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 36.0f});
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.toggle.apply({.alignSelf = scene::align::kMiddle});
      parts.toggle.setOnNow(facts.forum);
      if (facts.holds_spaces)
        fState.apply({.alpha = 0.4f, .disabled = true});
    }
  };
  // A space's rooms out of Home, or in it: a switch, for a space that is
  // not shown as one chat.
  struct flip_home_hide_act {
    Actions* actions;
    std::string room;
    void operator()() const { actions->flip_home_hide(room); }
  };
  struct home_hide_row : nodes::Stack {
    struct parts_t {
      nodes::Text label{"Its rooms not in Home", 14.0f, text_colour};
      widgets::Toggle<flip_home_hide_act> toggle;
    } parts;
    home_hide_row(Actions* a, const room_settings_facts& facts)
        : parts{.toggle = widgets::Toggle<flip_home_hide_act>({a, facts.id})} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 36.0f});
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.toggle.apply({.alignSelf = scene::align::kMiddle});
      parts.toggle.setOnNow(facts.hidden_from_home);
    }
  };
  struct general_page : nodes::Stack {
    using buttons_row = dialog_buttons<cancel_general, save_general>;
    struct parts_t {
      nodes::Text heading = tab_heading("General");
      avatar_mark photo;
      field name;
      field topic;
      buttons_row buttons;
      nodes::Text addresses = part_heading("Room Addresses");
      nodes::Text published = part_heading("Published Addresses");
      nodes::Text published_about = explained(
          "Published addresses can be used by anyone on any server to join your room. To publish an address, it "
          "needs to be set as a local address first.");
      nodes::Text main_address;
      nodes::Text others_title{"Other published addresses:", 14.0f, text_colour};
      std::vector<nodes::Text> others;
      nodes::Text other = part_heading("Other");
      nodes::Text events_about = explained("Room events shown in this room, for you: Default is as your account's.");
      event_kind_list<Actions> events;
      receipts_choice<Actions> receipts;
      previews_choice<Actions> previews;
      previews_direct_choice<Actions> previews_direct;
      typing_choice<Actions> typing;
      jump_search_choice<Actions> jump_search;
      nodes::Text forum_heading = part_heading("Shown as");
      forum_row forum;
      nodes::Text forum_about;
      home_hide_row home_hide;
      nodes::Text leave_heading = part_heading("Leave room");
      widgets::Button<ask<Actions, &Actions::leave_chat>> leave;
    } parts;
    general_page(Actions* a, room_settings* box, const room_settings_facts& facts)
        : parts{.photo = avatar_mark(facts.id, facts.name, 88.0f),
                .name = field("Room Name", "", facts.name),
                .topic = field("Room Topic", "", facts.topic),
                .buttons = buttons_row("Save", {box}, {box}),
                .main_address = nodes::Text(
                    "Main address: " + facts.alias.value_or("none"), 14.0f,
                    text_colour),
                .events = event_kind_list<Actions>(a, choice_level::chat{}, facts.events_all, facts.event_kinds),
                .receipts = receipts_choice<Actions>(a, choice_level::chat{}, facts.receipts),
                .previews = previews_choice<Actions>(a, choice_level::chat{}, facts.previews),
                .previews_direct = previews_direct_choice<Actions>(a, choice_level::chat{}, facts.previews_direct),
                .typing = typing_choice<Actions>(a, choice_level::chat{}, facts.typing),
                .jump_search = jump_search_choice<Actions>(a, choice_level::chat{}, facts.jump_search),
                .forum = forum_row(a, facts),
                .forum_about = explained(facts.holds_spaces
                                             ? "A space that holds spaces is shown as a space."
                                             : "On: in the chat list as one chat; its rooms open inside it, as Telegram's topics."),
                .home_hide = home_hide_row(a, facts),
                .leave = widgets::Button<ask<Actions, &Actions::leave_chat>>("Leave room", {a})} {
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.forum_heading, &parts.forum, &parts.forum_about})
        each->setVisible(facts.space);
      // A space's own: its rooms out of Home -- not one shown as one chat,
      // whose rooms are in it, not in the list.
      parts.home_hide.setVisible(facts.space && !facts.forum);
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
      parts.photo.apply({.alignSelf = scene::align::kStart});
      const bool rename = facts.may(power_need::rename{});
      const bool retopic = facts.may(power_need::retopic{});
      parts.name.apply({.disabled = !rename});
      parts.topic.apply({.disabled = !retopic});
      parts.buttons.setVisible(rename || retopic);
      parts.main_address.setWrapped(true);
      parts.main_address.apply({.fillX = true});
      for (const std::string& one : facts.other_aliases)
        parts.others.emplace_back(one, 14.0f, text_colour);
      if (facts.other_aliases.empty())
        parts.others.emplace_back("No other published addresses yet.", 13.0f, dim_colour);
      parts.leave.apply({.width = 130.0f, .height = 32.0f});
    }
  };

  // ---- Security & Privacy ------------------------------------------------------
  struct security_page : nodes::Stack {
    using join_choice = radio_choice<choose_join>;
    using history_choice = radio_choice<choose_history>;
    struct parts_t {
      nodes::Text heading = tab_heading("Security & Privacy");
      nodes::Text encryption = part_heading("Encryption");
      nodes::Text encryption_about = explained("Once enabled, encryption cannot be disabled.");
      toggle_line<turn_encryption_on> encrypted;
      nodes::Text encryption_warning;
      nodes::Text access = part_heading("Access");
      nodes::Text access_about;
      join_choice invite, knock, open;
      nodes::Text history = part_heading("Who can read history?");
      nodes::Text history_about = explained(
          "Changes to who can read history will only apply to future messages in this room. The visibility of "
          "existing history will be unchanged.");
      history_choice anyone, shared, invited, joined;
    } parts;
    security_page(Actions*, room_settings* box, const room_settings_facts& facts)
        : parts{.encrypted = toggle_line<turn_encryption_on>("Encrypted", {box}, facts.encrypted,
                                                             !facts.encrypted && facts.may(power_need::encrypt{})),
                .encryption_warning = nodes::Text(box->confirming_encryption
                                                      ? "Press again to enable encryption. mux cannot read encrypted "
                                                        "rooms yet: what is sent after this will not show here."
                                                      : "",
                                                  13.0f, error_colour),
                .access_about = explained("Decide who can join " + facts.name + "."),
                .invite = join_choice("Private (invite only)", "Only invited people can join.",
                                      {box, join_rule::invite{}}, is<join_rule::invite>(facts.join_rule),
                                      facts.may(power_need::change_access{})),
                .knock = join_choice("Ask to join", "People cannot join unless access is granted.",
                                     {box, join_rule::knock{}}, is<join_rule::knock>(facts.join_rule),
                                     facts.may(power_need::change_access{})),
                .open = join_choice("Public", "Anyone can find and join.", {box, join_rule::open{}},
                                    is<join_rule::open>(facts.join_rule), facts.may(power_need::change_access{})),
                .anyone = history_choice("Anyone", "", {box, history_rule::world_readable{}},
                                         is<history_rule::world_readable>(facts.history),
                                         facts.may(power_need::change_history{})),
                .shared = history_choice("Members only (since the point in time of selecting this option)", "",
                                         {box, history_rule::shared{}}, is<history_rule::shared>(facts.history),
                                         facts.may(power_need::change_history{})),
                .invited = history_choice("Members only (since they were invited)", "", {box, history_rule::invited{}},
                                          is<history_rule::invited>(facts.history),
                                          facts.may(power_need::change_history{})),
                .joined = history_choice("Members only (since they joined)", "", {box, history_rule::joined{}},
                                         is<history_rule::joined>(facts.history),
                                         facts.may(power_need::change_history{}))} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
      parts.encryption_warning.setWrapped(true);
      parts.encryption_warning.apply({.fillX = true});
      parts.encryption_warning.setVisible(box->confirming_encryption);
    }
  };

  // ---- Roles & Permissions ------------------------------------------------------
  // A level chosen of three, as Element's selects: Default, Moderator, Admin.
  template <class Act, class Make>
  struct level_choice : nodes::Stack {
    struct parts_t {
      segment<Act> fallback, moderator, admin;
      nodes::Text custom;
    } parts;
    level_choice(Make make, std::int64_t now, std::int64_t fallback, bool allowed)
        : parts{.fallback = segment<Act>("Default", make(fallback)),
                .moderator = segment<Act>("Moderator", make(50)),
                .admin = segment<Act>("Admin", make(100)),
                .custom = nodes::Text("", 12.0f, dim_colour)} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle, .disabled = !allowed});
      parts.fallback.set_active(now == fallback);
      parts.moderator.set_active(now == 50 && fallback != 50);
      parts.admin.set_active(now == 100 && fallback != 100);
      const bool custom = now != fallback && now != 50 && now != 100;
      parts.custom.setText(!custom ? std::string() : now == kCreatorPower ? std::string("Creator") : std::format("Custom ({})", now));
      parts.custom.setVisible(custom);
      parts.custom.apply({.alignSelf = scene::align::kMiddle});
      if (!allowed)
        fState.setAlpha(0.55f);
    }
  };
  struct need_maker {
    room_settings* box;
    power_need_t need;
    set_need operator()(std::int64_t level) const { return {box, need, level}; }
  };
  struct user_maker {
    room_settings* box;
    std::string user;
    set_level operator()(std::int64_t level) const { return {box, user, level}; }
  };
  struct event_maker {
    room_settings* box;
    std::string event;
    set_event_level operator()(std::int64_t level) const { return {box, event, level}; }
  };
  // A kind of event the list does not name, as the power levels set it.
  struct event_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      level_choice<set_event_level, event_maker> levels;
    } parts;
    event_row(room_settings* box, const std::string& event, std::int64_t level, const room_settings_facts& facts)
        : parts{.label = nodes::Text(event, 14.0f, text_colour),
                .levels = level_choice<set_event_level, event_maker>(
                    event_maker{box, event}, level, facts.needs.state_default,
                    facts.may(power_need::change_permissions{}) && level <= facts.mine)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 4.0f, 0.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  // Any kind of event, by its type: the level it asks, set.
  struct new_event_row : nodes::Stack {
    struct parts_t {
      field event;
      segment<pick_new_level> moderator, admin;
      widgets::Button<add_event_need> apply;
    } parts;
    explicit new_event_row(room_settings* box)
        : parts{.event = field("", "Event type, as m.room.server_acl"),
                .moderator = segment<pick_new_level>("Moderator", {box, 50}),
                .admin = segment<pick_new_level>("Admin", {box, 100}),
                .apply = widgets::Button<add_event_need>("Apply", {box})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.event.apply({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kEnd});
      parts.moderator.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.admin.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.apply.setPrimary(true);
      parts.apply.apply({.width = 80.0f, .height = 30.0f, .alignSelf = scene::align::kEnd,
                         .margin = {0.0f, 0.0f, 3.0f, 0.0f}});
    }
  };
  struct permission_row : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      level_choice<set_need, need_maker> levels;
    } parts;
    permission_row(room_settings* box, std::string text, power_need_t need, const room_settings_facts& facts)
        : parts{.label = nodes::Text(std::move(text), 14.0f, text_colour),
                .levels = level_choice<set_need, need_maker>(
                    need_maker{box, need}, facts.needs.of(need), facts.needs.users_default,
                    facts.may(power_need::change_permissions{}) && facts.needs.of(need) <= facts.mine)} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 0.0f, 4.0f, 0.0f}});
      parts.label.setWrapped(true);
      parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
  };
  struct privileged_row : nodes::Stack {
    struct parts_t {
      avatar_mark face;
      two_lines texts;
      level_choice<set_level, user_maker> levels;
    } parts;
    privileged_row(room_settings* box, const room_settings_facts::person& one, const room_settings_facts& facts)
        : parts{.face = avatar_mark(one.id, one.name, 32.0f),
                .texts = two_lines(one.name, one.id, 14.0f, 2.0f),
                .levels = level_choice<set_level, user_maker>(
                    user_maker{box, one.id}, one.level, facts.needs.users_default,
                    facts.may(power_need::change_permissions{}) && (one.level < facts.mine))} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .height = 48.0f});
    }
  };
  struct new_level_row : nodes::Stack {
    struct parts_t {
      field user;
      segment<pick_new_level> moderator, admin;
      widgets::Button<add_privileged> apply;
    } parts;
    explicit new_level_row(room_settings* box)
        : parts{.user = field("", "User ID, as @someone:server"),
                .moderator = segment<pick_new_level>("Moderator", {box, 50}),
                .admin = segment<pick_new_level>("Admin", {box, 100}),
                .apply = widgets::Button<add_privileged>("Apply", {box})} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      parts.user.apply({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX, .alignSelf = scene::align::kEnd});
      parts.moderator.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.admin.apply({.alignSelf = scene::align::kEnd, .margin = {0.0f, 0.0f, 4.0f, 0.0f}});
      parts.apply.setPrimary(true);
      parts.apply.apply({.width = 80.0f, .height = 30.0f, .alignSelf = scene::align::kEnd,
                         .margin = {0.0f, 0.0f, 3.0f, 0.0f}});
    }
  };
  struct roles_page : nodes::Stack {
    struct parts_t {
      nodes::Text heading = tab_heading("Roles & Permissions");
      nodes::Text privileged = part_heading("Privileged Users");
      std::vector<privileged_row> users;
      nodes::Text none_privileged = explained("No users have specific privileges in this room.");
      nodes::Text add = part_heading("Add privileged users");
      nodes::Text add_about = explained("Give one or multiple users in this room more privileges.");
      new_level_row adding;
      nodes::Text permissions = part_heading("Permissions");
      nodes::Text permissions_about = explained("Select the roles required to change various parts of the room.");
      std::vector<permission_row> rows;
      // Every other kind of event the power levels set, and any kind added.
      std::vector<event_row> others;
      nodes::Text add_event = part_heading("Any other event");
      new_event_row adding_event;
    } parts;
    roles_page(Actions*, room_settings* box, const room_settings_facts& facts)
        : parts{.adding = new_level_row(box), .adding_event = new_event_row(box)} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
      parts.users.reserve(facts.privileged.size());
      for (const auto& one : facts.privileged)
        parts.users.emplace_back(box, one, facts);
      parts.none_privileged.setVisible(facts.privileged.empty());
      const bool may_add = facts.may(power_need::change_permissions{});
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.add, &parts.add_about, &parts.adding})
        each->setVisible(may_add);
      // Element's list, in its order and words.
      const std::pair<const char*, power_need_t> all[] = {
          {"Default role", power_need::default_role{}},
          {"Send messages", power_need::send_messages{}},
          {"Invite users", power_need::invite{}},
          {"Change settings", power_need::change_settings{}},
          {"Remove users", power_need::kick{}},
          {"Ban users", power_need::ban{}},
          {"Remove messages sent by others", power_need::redact{}},
          {"Notify everyone", power_need::notify_everyone{}},
          {"Change avatar", power_need::change_avatar{}},
          {"Change room name", power_need::rename{}},
          {"Change main address for the room", power_need::change_address{}},
          {"Change history visibility", power_need::change_history{}},
          {"Change who can join", power_need::change_access{}},
          {"Change permissions", power_need::change_permissions{}},
          {"Change topic", power_need::retopic{}},
          {"Upgrade the room", power_need::upgrade{}},
          {"Enable room encryption", power_need::encrypt{}},
          {"Change server ACLs", power_need::change_acl{}},
          {"Manage pinned events", power_need::pin{}},
      };
      parts.rows.reserve(std::size(all));
      for (const auto& [text, need] : all)
        parts.rows.emplace_back(box, text, need, facts);
      // Those the list has by their own row are not again.
      std::set<std::string_view> listed;
      for (const auto& [text, need] : all)
        splice::visit(splice::overloaded{[&]<sends_state Need>(const Need&) { listed.insert(Need::event); }, [](const auto&) {}}, need);
      for (const auto& [event, level] : facts.needs.events)
        if (!listed.contains(event))
          parts.others.emplace_back(box, event, level, facts);
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.add_event, &parts.adding_event})
        each->setVisible(may_add);
    }
  };

  // ---- Notifications ----------------------------------------------------------------
  struct notifications_page : nodes::Stack {
    using choice = radio_choice<notify_as>;
    struct parts_t {
      nodes::Text heading = tab_heading("Notifications");
      choice by_default, all, mentions, off;
    } parts;
    notifications_page(Actions*, room_settings* box, const room_settings_facts& facts)
        : parts{.by_default = choice("Default", "As your account's notifications are set up",
                                     {box, config::notify_mode::by_default{}},
                                     is<config::notify_mode::by_default>(facts.notify_mode), true),
                .all = choice("All messages", "Get notified of every message", {box, config::notify_mode::all{}},
                              is<config::notify_mode::all>(facts.notify_mode), true),
                .mentions = choice("@mentions & keywords", "Get notified only with mentions and keywords",
                                   {box, config::notify_mode::mentions{}},
                                   is<config::notify_mode::mentions>(facts.notify_mode), true),
                .off = choice("Off", "You won't get any notifications", {box, config::notify_mode::off{}},
                              is<config::notify_mode::off>(facts.notify_mode), true)} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    }
  };

  // ---- Advanced -------------------------------------------------------------------
  struct advanced_page : nodes::Stack {
    struct parts_t {
      nodes::Text heading = tab_heading("Advanced");
      nodes::Text information = part_heading("Room information");
      copy_line id;
      nodes::Text version;
      // Upgraded, as Element's: the version to go to, and the button. The
      // server makes the new room and tombstones this one.
      field upgrade_to;
      widgets::Button<upgrade_press> upgrade;
      nodes::Text tools = part_heading("Developer tools");
      widgets::Button<ask<Actions, &Actions::explore_state>> explore;
      widgets::Button<ask<Actions, &Actions::open_send_custom>> send_custom;
      nodes::Text packs_heading = part_heading("Emojis & Stickers");
      widgets::Button<ask<Actions, &Actions::open_room_packs>> packs;
    } parts;
    advanced_page(Actions* a, room_settings* box, const room_settings_facts& facts)
        : parts{.id = copy_line("Internal room ID", facts.id),
                .version = nodes::Text("Room version: " + facts.version, 14.0f, text_colour),
                .upgrade_to = field("Upgrade to room version", "12", "12"),
                .upgrade = widgets::Button<upgrade_press>("Upgrade this room", {box}),
                .explore = widgets::Button<ask<Actions, &Actions::explore_state>>("Explore room state", {a}),
                .send_custom = widgets::Button<ask<Actions, &Actions::open_send_custom>>("Send custom event", {a}),
                .packs = widgets::Button<ask<Actions, &Actions::open_room_packs>>("Edit room packs", {a})} {
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.explore, &parts.send_custom, &parts.packs, &parts.upgrade})
        each->apply({.width = 180.0f, .height = 32.0f});
      const bool may = facts.may(power_need::upgrade{});
      parts.upgrade_to.setVisible(may);
      parts.upgrade.setVisible(may);
    }
  };

  // ---- Appearance: the room's background, bubbles and panels --------------------
  struct looks_page : nodes::Stack {
    struct parts_t {
      nodes::Text heading = tab_heading("Appearance");
      look_choices<Actions> choices;
    } parts;
    looks_page(Actions* a, room_settings*, const room_settings_facts&) : parts{.choices = look_choices<Actions>(a, choice_level::chat{})} {
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 28.0f, 24.0f, 12.0f}});
    }
  };

  using page_t = splice::variant<general_page, security_page, roles_page, notifications_page, advanced_page, looks_page>;
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
  };
  struct parts_t {
    header_t header;
    body_row body;
  } parts;

  settings_tab_t tab = settings_tab::general{};
  bool confirming_encryption = false;
  std::int64_t new_level = 50;
  // A tab to be made again, at the next frame: not from inside a press on
  // what it replaces.
  bool rebuild_due = false;
  bool to_top = false;  // another tab: shown from its top

  room_settings(Actions* a, const room_settings_facts& shown)
      : actions(a), facts(shown),
        parts{.header = header_t("Room Settings - " + shown.name, {}, {a}, false, true),
              .body = body_row(this, page_t(std::in_place_index<0>, a, this, shown))} {
    fState.apply({.fill = true});
    parts.body.parts.tabs.show(tab);
    this->show_new_level();
  }

  [[nodiscard]] page_holder& holder() { return std::get<0>(parts.body.parts.content.fChildren); }
  // A tab shown, made from the facts as they now are -- at the next frame.
  void show_tab(const settings_tab_t& to) {
    tab = to;
    rebuild_due = true;
    this->markDamaged();
  }
  [[nodiscard]] bool settling() const { return rebuild_due; }
  void update(double) {
    if (std::exchange(rebuild_due, false))
      this->rebuild();
  }
  void rebuild() {
    const settings_tab_t to = tab;
    splice::visit(splice::overloaded{
                   [&](settings_tab::general) { holder().parts.page.template emplace<0>(actions, this, facts); },
                   [&](settings_tab::security) { holder().parts.page.template emplace<1>(actions, this, facts); },
                   [&](settings_tab::roles) { holder().parts.page.template emplace<2>(actions, this, facts); },
                   [&](settings_tab::notifications) { holder().parts.page.template emplace<3>(actions, this, facts); },
                   [&](settings_tab::advanced) { holder().parts.page.template emplace<4>(actions, this, facts); },
                   [&](settings_tab::looks) { holder().parts.page.template emplace<5>(actions, this, facts); }},
               to);
    parts.body.parts.tabs.show(tab);
    if (std::exchange(to_top, false))
      parts.body.parts.content.scrollTo(0.0f);
    this->show_new_level();
    this->invalidateLayout();
  }

  // What is done: asked of the program, and the facts kept as they will be.
  void store_general() {
    splice::visit(splice::overloaded{[&](general_page& page) {
                            const std::string& name = page.parts.name.text();
                            const std::string& topic = page.parts.topic.text();
                            if (name != facts.name && facts.may(power_need::rename{})) {
                              actions->room_act(room_action::rename{name});
                              facts.name = name;
                            }
                            if (topic != facts.topic && facts.may(power_need::retopic{})) {
                              actions->room_act(room_action::retopic{topic});
                              facts.topic = topic;
                            }
                          },
                          [](auto&) {}},
               holder().parts.page);
  }
  void chose(const join_rule_t& rule) {
    if (!facts.may(power_need::change_access{}))
      return;
    facts.join_rule = rule;
    actions->room_act(room_action::set_join_rule{rule});
    this->show_tab(tab);
  }
  void chose(const history_rule_t& rule) {
    if (!facts.may(power_need::change_history{}))
      return;
    facts.history = rule;
    actions->room_act(room_action::set_history{rule});
    this->show_tab(tab);
  }
  // Encryption: asked twice, as Element asks before it -- once on, it
  // cannot be turned off.
  void encrypt() {
    if (facts.encrypted || !facts.may(power_need::encrypt{}))
      return;
    if (!confirming_encryption) {
      confirming_encryption = true;
    } else {
      confirming_encryption = false;
      facts.encrypted = true;
      actions->room_act(room_action::encrypt{});
    }
    this->show_tab(tab);
  }
  void needs_level(const power_need_t& need, std::int64_t level) {
    if (!facts.may(power_need::change_permissions{}) || level > facts.mine)
      return;
    actions->room_act(room_action::set_need{need, level});
    splice::visit(splice::overloaded{[&](power_need::default_role) { facts.needs.users_default = level; },
                          [&](power_need::send_messages) { facts.needs.events_default = level; },
                          [&](power_need::change_settings) { facts.needs.state_default = level; },
                          [&](power_need::invite) { facts.needs.invite = level; },
                          [&](power_need::kick) { facts.needs.kick = level; },
                          [&](power_need::ban) { facts.needs.ban = level; },
                          [&](power_need::redact) { facts.needs.redact = level; },
                          [&](power_need::notify_everyone) { facts.needs.notify_room = level; },
                          [&]<sends_state Need>(Need) { facts.needs.events.insert_or_assign(std::string(Need::event), level); }},
               need);
    this->show_tab(tab);
  }
  void user_level(const std::string& user, std::int64_t level) {
    if (!facts.may(power_need::change_permissions{}) || level > facts.mine)
      return;
    actions->room_act(room_action::set_power{user, level});
    const auto found = std::ranges::find(facts.privileged, user, &room_settings_facts::person::id);
    if (found != facts.privileged.end())
      found->level = level;
    this->show_tab(tab);
  }
  void show_new_level() {
    splice::visit(splice::overloaded{[&](roles_page& page) {
                            page.parts.adding.parts.moderator.set_active(new_level == 50);
                            page.parts.adding_event.parts.moderator.set_active(new_level == 50);
                            page.parts.adding_event.parts.admin.set_active(new_level == 100);
                            page.parts.adding.parts.admin.set_active(new_level == 100);
                          },
                          [](auto&) {}},
               holder().parts.page);
  }
  // Upgraded to the version written: asked of the server.
  void upgrade() {
    if (!facts.may(power_need::upgrade{}))
      return;
    std::string version;
    splice::visit(splice::overloaded{[&](advanced_page& page) { version = page.parts.upgrade_to.text(); }, [](auto&) {}},
                  holder().parts.page);
    if (!version.empty())
      actions->room_act(room_action::upgrade{version});
  }
  // Any kind of event's level: asked, and shown so at once.
  void event_level(const std::string& event, std::int64_t level) {
    if (!facts.may(power_need::change_permissions{}) || level > facts.mine || event.empty())
      return;
    actions->room_act(room_action::set_event_need{event, level});
    facts.needs.events.insert_or_assign(event, level);
    this->show_tab(tab);
  }
  void apply_event_level() {
    std::string event;
    splice::visit(splice::overloaded{[&](roles_page& page) { event = page.parts.adding_event.parts.event.text(); }, [](auto&) {}},
                  holder().parts.page);
    this->event_level(event, new_level);
  }
  void apply_new_level() {
    std::string user;
    splice::visit(splice::overloaded{[&](roles_page& page) { user = page.parts.adding.parts.user.text(); }, [](auto&) {}},
               holder().parts.page);
    if (user.empty())
      return;
    if (std::ranges::find(facts.privileged, user, &room_settings_facts::person::id) == facts.privileged.end())
      facts.privileged.push_back({user, user, facts.needs.users_default});
    this->user_level(user, new_level);
  }
  void notify(const config::notify_mode_t& mode) {
    facts.notify_mode = mode;
    actions->set_chat_notify(mode);
    this->show_tab(tab);
  }

  template <class Rule, class Variant>
  [[nodiscard]] static bool is(const Variant& now) {
    return splice::visit(splice::overloaded{[](const Rule&) { return true; }, [](const auto&) { return false; }}, now);
  }
};

}  // namespace mux::ui
