// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:controls -- The small controls: marks, rows, icon buttons, headers, segments, the drawer button.
export module mux.ui:controls;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.text;
import skiff.widgets.avatar;
import mux.core;
import mux.logic.room_events;
import mux.config;
import :base;
import :icons;
import :avatars;
import :themes;

export namespace mux::ui {

// A line of a list or a menu, as wide as what holds it and square: an icon
// on the left, its text, and a radio mark on the right where it is one of a
// choice. It lights under the pointer; a press does `act`.
// An icon on its own, in a row: drawn, not pressed.
struct icon_mark : nodes::Icon {
  explicit icon_mark(icon_t mark = icon::none{}) : nodes::Icon(shape_of(mark), dim_colour) {
    fState.apply({.width = 28.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle});
  }
};
// A radio's ring, with a dot in it while it is the one chosen.
struct radio_mark : nodes::Icon {
  bool on = false;
  radio_mark() : nodes::Icon(shape(false), dim_colour) {
    fState.apply({.width = 20.0f, .height = 20.0f, .alignSelf = scene::align::kMiddle});
  }
  // A ring, and a dot in it while chosen.
  static IconShape shape(bool chosen) {
    IconShape out{{{nodes::mark::circle{0.0f, 0.0f, 8.0f}, 2.0f}}};
    if (chosen)
      out.marks.push_back({nodes::mark::circle{0.0f, 0.0f, 4.0f}, 0.0f, true});
    return out;
  }
  void set_on(bool chosen) {
    on = chosen;
    this->setShape(shape(chosen));
    this->setColour(chosen ? accent_colour : dim_colour);
  }
};
// A round avatar of a size, in a row.
struct avatar_mark : widgets::Avatar<from_avatars> {
  std::string key;
  avatar_mark(std::string id, std::string shown, float size)
      : widgets::Avatar<from_avatars>(initials_of(shown), size, picture_of(id), gradient_of(id)), key(std::move(id)) {
    fState.apply({.alignSelf = scene::align::kMiddle});
  }
  // Another's: a chat's or a person's, by their id and name.
  void show(std::string id, std::string_view shown) {
    key = std::move(id);
    widgets::Avatar<from_avatars>::show(initials_of(shown), picture_of(key), gradient_of(key));
  }
};
// An avatar that opens: pressed, its picture in the viewer, where it can be
// looked at whole and saved -- a person's in their card, a chat's in its
// info.
template <class Actions>
struct avatar_button : avatar_mark {
  Actions* actions = nullptr;
  avatar_button(Actions* a, std::string id, std::string shown, float size)
      : avatar_mark(std::move(id), shown, size), actions(a) {
    fState.setCursor(scene::cursor::hand{});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    if (!key.empty())
      actions->open_avatar(key);
    return true;
  }
};
// A name over how it is: two lines, each cut where it runs out of room,
// taking what their row leaves them.
struct two_lines : nodes::Stack {
  struct parts_t {
    nodes::Text name;
    nodes::Text state;
  } parts;
  two_lines(std::string first, std::string second, float size, float gap)
      : parts{.name = nodes::Text(std::move(first), size, text_colour, true),
              .state = nodes::Text(std::move(second), size - 2.0f, dim_colour)} {
    this->setGap(gap);
    fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    for (nodes::Text* each : {&parts.name, &parts.state}) {
      each->setElided(true);
      each->apply({.fillX = true});
    }
    // Nothing to say under the name -- no presence known, no role: no line
    // kept for it, the name alone in the middle.
    parts.state.setVisible(!parts.state.text().empty());
  }
};

template <class Act>
struct row_item : nodes::Stack {
  Act act;
  // Whether it is one of a choice, and the chosen one.
  std::optional<bool> radio;
  struct parts_t {
    icon_mark mark;
    nodes::Text label;
    radio_mark dot;
  } parts;

  static constexpr float kHeight = 46.0f;

  // Declared: its icon, its text taking the room, and a radio at the end.
  row_item(std::string text, Act what, icon_t icon = icon::none{}, std::optional<bool> choice = std::nullopt)
      : act(std::move(what)), radio(choice),
        parts{.mark = icon_mark(icon), .label = nodes::Text(std::move(text), 15.0f, text_colour)} {
    auto& [mark, label, dot] = parts;
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}, .hoverBackground = chosen_colour, .selectedBackground = chosen_colour, .focusBackground = chosen_colour});
    mark.setVisible(splice::visit([](auto one) { return drawn(one); }, icon));
    label.setElided(true);
    label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    dot.set_on(choice.value_or(false));
    dot.setVisible(choice.has_value());
  }

  void set_chosen(bool on) {
    radio = on;
    parts.dot.set_on(on);
    parts.dot.setVisible(true);
    parts.dot.markDamaged();
  }
  // Lit as the line whose page is shown beside the list.
  void set_lit(bool on) {
    lit = on;
    fState.apply({.selected = on});
    this->markDamaged();
  }
  bool lit = false;

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
    out.fSelected = radio.value_or(false);
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// A round button with only an icon in it: back, close.
template <class Act>
struct icon_button : scene::Node {
  Act act;
  struct parts_t {
    nodes::Icon mark;
  } parts;

  // Round, lit under the pointer or the keyboard's focus.
  icon_button(icon_t mark, Act what) : act(std::move(what)), parts{.mark = nodes::Icon(shape_of(mark), text_colour)} {
    fState.apply({.width = 36.0f,
                  .height = 36.0f,
                  .cornerRadius = 18.0f,
                  .hoverBackground = chosen_colour,
                  .focusBackground = chosen_colour});
    parts.mark.apply({.fill = true});
  }
  void set_colour(skia::SkColor colour) { parts.mark.setColour(colour); }

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
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// The head of a page: ← on the left where there is somewhere to go back to,
// the page's name, and ✕ on the right where the page closes.
template <class Back, class Close>
struct page_header : nodes::Stack {
  struct parts_t {
    icon_button<Back> back;
    nodes::Text title;
    icon_button<Close> close;
  } parts;

  static constexpr float kHeight = 54.0f;

  page_header(std::string name, Back to, Close shut, bool has_back, bool has_close)
      : parts{.back = icon_button<Back>(icon::back{}, std::move(to)),
              .title = nodes::Text(std::move(name), 17.0f, text_colour, true),
              .close = icon_button<Close>(icon::close{}, std::move(shut))} {
    auto& [back, title, close] = parts;
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
    back.setVisible(has_back);
    close.setVisible(has_close);
    back.apply({.alignSelf = scene::align::kMiddle});
    close.apply({.alignSelf = scene::align::kMiddle});
    title.setElided(true);
    title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                 .margin = {0.0f, 0.0f, 0.0f, has_back ? 0.0f : 10.0f}});
  }
};

// One segment of a segmented control: square, its text centred, filled
// with the accent while it is the one chosen.
template <class Act>
struct segment : nodes::Stack {
  Act act;
  bool active = false;
  struct parts_t {
    nodes::Text label;
  } parts;

  segment(std::string text, Act what)
      : act(std::move(what)), parts{.label = nodes::Text(std::move(text), 13.0f, text_colour, true)} {
    fState.apply({.width = 92.0f, .height = 28.0f, .hoverBackground = chosen_colour, .selectedBackground = accent_colour, .focusBackground = chosen_colour});
    fStack.justify = nodes::justify::middle{};
    parts.label.apply({.alignSelf = scene::align::kMiddle});
  }

  void set_active(bool on) {
    active = on;
    parts.label.setColour(on ? on_accent_colour : text_colour);
    fState.apply({.selected = on});
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
    out.fRole = scene::semantic_role::tab{};
    out.fLabel = parts.label.text();
    out.fSelected = active;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// ---- the drawer's button ------------------------------------------------------

// Three lines at the top-left of the conversation list: a press pulls the
// drawer out.
template <class Actions>
struct menu_button : scene::Node {
  // Three bars, and a plate under them while it is hovered or focused.
  struct parts_t {
    nodes::Icon bars;
  } parts{.bars = nodes::Icon(IconShape{{{nodes::mark::rect{-8.0f, -7.0f, 8.0f, -5.0f, 1.0f}, 0.0f, true},
                                         {nodes::mark::rect{-8.0f, -1.0f, 8.0f, 1.0f, 1.0f}, 0.0f, true},
                                         {nodes::mark::rect{-8.0f, 5.0f, 8.0f, 7.0f, 1.0f}, 0.0f, true}}},
                              text_colour)};
  Actions* actions = nullptr;

  explicit menu_button(Actions* a) : actions(a) {
    fState.apply({.width = 36.0f, .height = 36.0f, .cornerRadius = 8.0f, .hoverBackground = chosen_colour,
                  .focusBackground = chosen_colour});
    parts.bars.apply({.fill = true});
  }


  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->open_drawer();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = "Menu";
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// A kind of room event, as its row names it.
[[nodiscard]] constexpr std::string_view label_of(room_event::joins) { return "Joins and leaves"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::invites) { return "Invitations, removals and bans"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::names) { return "Name changes"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::avatars) { return "Picture changes"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::room_name) { return "Room name"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::topic) { return "Topic"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::room_avatar) { return "Room picture"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::address) { return "Room address"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::pins) { return "Pinned messages"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::permissions) { return "Permissions"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::access) { return "Who can join and read"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::encryption) { return "Encryption"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::other) { return "Everything else"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::reactions) { return "Reactions, each as a line"; }
[[nodiscard]] constexpr std::string_view label_of(room_event::unreactions) { return "Reactions taken back"; }

// A choice among a few, as a dropdown: a button saying the one in use and
// a chevron; pressed, the options open under it, in the page, the one in
// use marked; one pressed, chosen, and the list closed. The menu takes the
// presses its parts let by -- it holds whether it is open, and nothing of
// it is pointed at from its parts.
template <class Choose>
struct choice_menu : nodes::Stack {
  Choose choose;  // told the index of the option pressed
  bool open = false;
  struct head_t : nodes::Stack {
    struct parts_t {
      nodes::Text label;
      nodes::Text value;
      nodes::Icon chevron;
    } parts;
    head_t(std::string label, std::string value)
        : parts{.label = nodes::Text(std::move(label), 13.0f, dim_colour),
                .value = nodes::Text(std::move(value), 14.0f, text_colour),
                .chevron = nodes::Icon(shape_of(icon::down{}), dim_colour)} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f,
                    .background = tile_colour, .hoverBackground = chosen_colour, .border = scene::Border{band_colour, 1.0f}});
      parts.label.apply({.alignSelf = scene::align::kMiddle});
      parts.label.setVisible(!parts.label.text().empty());
      parts.value.setElided(true);
      parts.value.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.chevron.apply({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  };
  struct option_t : nodes::Stack {
    struct parts_t {
      nodes::Text name;
    } parts;
    option_t(std::string name, bool chosen) : parts{.name = nodes::Text(std::move(name), 14.0f, chosen ? accent_colour : text_colour, chosen)} {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 32.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f,
                    .hoverBackground = chosen_colour});
      parts.name.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  };
  struct parts_t {
    head_t head;
    std::vector<option_t> options;
  } parts;
  choice_menu(std::string label, const std::vector<std::string>& names, std::size_t current, Choose c)
      : choose(std::move(c)), parts{.head = head_t(std::move(label), current < names.size() ? names[current] : std::string())} {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.options.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
      parts.options.emplace_back(names[i], i == current);
      parts.options.back().setVisible(false);
    }
  }
  void show_options(bool on) {
    open = on;
    for (option_t& each : parts.options)
      each.setVisible(on);
    this->invalidateLayout();
    this->markDamaged();
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float x, float y) {
    if (parts.head.bounds().contains(x, y)) {
      this->show_options(!open);
      return true;
    }
    for (std::size_t i = 0; i < parts.options.size(); ++i)
      if (parts.options[i].visible() && parts.options[i].bounds().contains(x, y)) {
        this->show_options(false);
        choose(i);
        return true;
      }
    return false;
  }
};

// The items of the space bars -- Home, Direct messages, each space -- each
// with where it is: the side bar, the top one, both, or hidden.
template <class Actions>
struct spaces_choices : nodes::Stack {
  struct pick_bars {
    Actions* actions;
    std::string account;
    config::space_item_t item;
    void operator()(std::size_t index) const {
      static constexpr std::array<std::pair<bool, bool>, 4> kWays{{{true, false}, {false, true}, {true, true}, {false, false}}};
      if (index < kWays.size())
        actions->set_space_bars(account, item, kWays[index].first, kWays[index].second);
    }
  };
  struct row : nodes::Stack {
    struct parts_t {
      nodes::Text name;
      choice_menu<pick_bars> where;
    } parts;
    row(Actions* a, const std::string& account, const space_item_shown& one)
        : parts{.name = nodes::Text(one.name, 14.0f, text_colour),
                .where = choice_menu<pick_bars>("", {"Side bar", "Top bar", "Both bars", "Hidden"},
                                                one.side && !one.top   ? 0
                                                : one.top && !one.side ? 1
                                                : one.side && one.top  ? 2
                                                                       : 3,
                                                pick_bars{a, account, one.item})} {
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 20.0f, 4.0f, 20.0f}});
    }
  };
  struct parts_t {
    std::vector<row> rows;
  } parts;
  explicit spaces_choices(Actions* a) {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(space_items_now().size());
    for (const space_item_shown& one : space_items_now())
      parts.rows.emplace_back(a, space_account_now(), one);
  }
};

// What each level holds of room events, as the program last said: for a
// list to show what is in effect where a level is not its own.
struct room_events_held {
  std::optional<bool> all;
  std::optional<config::room_event_kinds> kinds;
};
inline room_events_held& room_events_at(const choice_level_t& level) {
  static room_events_held everywhere, account, chat;
  return splice::visit(splice::overloaded{[](choice_level::everywhere) -> room_events_held& { return everywhere; },
                                          [](choice_level::account) -> room_events_held& { return account; },
                                          [](choice_level::chat) -> room_events_held& { return chat; }},
                       level);
}
// What is shown with a level as it is, and the levels over it.
[[nodiscard]] inline room_event_filter events_in_effect(const choice_level_t& level) {
  const room_events_held& every = room_events_at(choice_level::everywhere{});
  const room_events_held& account = room_events_at(choice_level::account{});
  const room_events_held& chat = room_events_at(choice_level::chat{});
  return splice::visit(
      splice::overloaded{[&](choice_level::everywhere) {
                           return logic::filter_of(std::nullopt, std::nullopt, std::nullopt, std::nullopt, every.kinds, every.all.value_or(true));
                         },
                         [&](choice_level::account) {
                           return logic::filter_of(std::nullopt, std::nullopt, account.kinds, account.all, every.kinds, every.all.value_or(true));
                         },
                         [&](choice_level::chat) {
                           return logic::filter_of(chat.kinds, chat.all, account.kinds, account.all, every.kinds, every.all.value_or(true));
                         }},
      level);
}
// And with the level as the one over it: what "As above" shows.
[[nodiscard]] inline room_event_filter events_above(const choice_level_t& level) {
  return splice::visit(splice::overloaded{[](choice_level::chat) { return events_in_effect(choice_level::account{}); },
                                          [](const auto&) { return events_in_effect(choice_level::everywhere{}); }},
                       level);
}

// Which room events show, at one level -- every account's, one's, a chat's:
// how, as a dropdown -- As above (under the top), All events, Messages only,
// or Custom -- and under it a row for each kind, Show or Hide. The rows say
// what is in effect however the level is set; they are chosen only where
// it is Custom, and greyed where not: nothing under a choice that overrides
// it looks as if it did something. Custom starts from what was in effect.
template <class Actions>
struct event_kind_list : nodes::Stack {
  struct row;
  struct choose {
    row* in = nullptr;
    bool show = true;
    void operator()() const { in->chose(show); }
  };
  struct row : nodes::Stack {
    Actions* actions = nullptr;
    choice_level_t level;
    room_event_t kind;
    bool live = false;
    struct parts_t {
      nodes::Text label;
      segment<choose> show, hide;
    } parts;
    row(Actions* a, choice_level_t at, room_event_t which, std::string_view text)
        : actions(a), level(at), kind(which),
          parts{.label = nodes::Text(std::string(text), 14.0f, text_colour),
                .show = segment<choose>("Show", {this, true}),
                .hide = segment<choose>("Hide", {this, false})} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (segment<choose>* each : {&parts.show, &parts.hide})
        each->apply({.width = 70.0f, .alignSelf = scene::align::kMiddle});
    }
    void show_value(bool on) {
      parts.show.set_active(on);
      parts.hide.set_active(!on);
    }
    void set_live(bool on) {
      live = on;
      fState.apply({.alpha = on ? 1.0f : 0.4f, .disabled = !on});
    }
    void chose(bool on) {
      if (!live)
        return;
      this->show_value(on);
      actions->set_room_event_kind(level, kind, on);
    }
  };
  // The ways a level can be, in the dropdown's order, from As above.
  static constexpr std::size_t kAbove = 0, kAll = 1, kMessages = 2, kCustom = 3;
  [[nodiscard]] static bool inherits(const choice_level_t& level) {
    return splice::visit(splice::overloaded{[](choice_level::everywhere) { return false; }, [](const auto&) { return true; }}, level);
  }
  [[nodiscard]] static std::size_t way_of(const choice_level_t& level, std::optional<bool> all,
                                          const std::optional<config::room_event_kinds>& kinds) {
    const bool custom = kinds && std::ranges::any_of(all_room_events, [&](const room_event_t& kind) {
                          return logic::choice_of(kinds, kind).has_value();
                        });
    if (custom)
      return kCustom;
    if (!all)
      return inherits(level) ? kAbove : kAll;
    return *all ? kAll : kMessages;
  }
  // A way chosen: the rows shown so and let be chosen or not, and the
  // program told what the level now holds.
  struct pick_way {
    Actions* actions;
    choice_level_t level;
    row* first;
    std::size_t count;
    void operator()(std::size_t index) const {
      const std::size_t way = index + (inherits(level) ? 0 : 1);
      const room_event_filter now = events_in_effect(level);
      std::optional<bool> all;
      std::optional<config::room_event_kinds> kinds;
      room_event_filter shown = now;
      if (way == kAbove) {
        shown = events_above(level);
      } else if (way == kAll) {
        all = true;
        shown.shown.fill(true);
      } else if (way == kMessages) {
        all = false;
        shown.shown.fill(false);
      } else {
        kinds.emplace();
        for (const room_event_t& kind : all_room_events)
          logic::choice_in(*kinds, kind) = now.shows(kind);
      }
      for (row& each : std::span(first, count)) {
        each.show_value(shown.shows(each.kind));
        each.set_live(way == kCustom);
      }
      actions->set_room_events(level, all, kinds);
    }
  };
  struct parts_t {
    std::optional<choice_menu<pick_way>> way;
    std::vector<row> rows;
  } parts;
  event_kind_list(Actions* a, choice_level_t level, std::optional<bool> all,
                  const std::optional<config::room_event_kinds>& kinds) {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    // Made where they stay: each row's switches know it by its address,
    // and the dropdown the rows by the first's.
    parts.rows.reserve(kRoomEventKinds);
    const std::size_t way = way_of(level, all, kinds);
    const room_event_filter shown = way == kAbove ? events_above(level)
                                    : way == kCustom ? events_in_effect(level)
                                                     : [&] {
                                                         room_event_filter one;
                                                         one.shown.fill(way == kAll);
                                                         return one;
                                                       }();
    for (const room_event_t& kind : all_room_events) {
      parts.rows.emplace_back(a, level, kind, splice::visit([](auto one) { return label_of(one); }, kind));
      parts.rows.back().show_value(shown.shows(kind));
      parts.rows.back().set_live(way == kCustom);
    }
    std::vector<std::string> names;
    if (inherits(level))
      names.emplace_back("As above");
    for (const char* name : {"All events", "Messages only", "Custom"})
      names.emplace_back(name);
    parts.way.emplace("Room events", names, way - (inherits(level) ? 0 : 1),
                      pick_way{a, level, parts.rows.data(), parts.rows.size()});
    parts.way->apply({.margin = {0.0f, 20.0f, 4.0f, 20.0f}});
  }
};

// How far a search for a message jumped to pages back, at one level: a few
// numbers of events and No limit, and, where a level under decides for it,
// Default.
template <class Actions>
struct jump_search_choice : nodes::Stack {
  static constexpr std::array<std::int64_t, 4> kChoices{500, 5000, 50000, 0};
  struct row;
  struct choose {
    row* in = nullptr;
    std::optional<std::int64_t> most;
    void operator()() const { in->chose(most); }
  };
  struct row : nodes::Stack {
    Actions* actions = nullptr;
    choice_level_t level;
    struct parts_t {
      nodes::Text label;
      segment<choose> fallback;
      std::vector<segment<choose>> choices;
    } parts;
    [[nodiscard]] static std::string label_of(std::int64_t most) {
      return most == 0 ? std::string("No limit") : std::format("{}", most);
    }
    row(Actions* a, choice_level_t at, std::optional<std::int64_t> now)
        : actions(a), level(at),
          parts{.label = nodes::Text("Look back for a message", 14.0f, text_colour),
                .fallback = segment<choose>("Default", {this, std::nullopt})} {
      const bool everywhere =
          splice::visit(splice::overloaded{[](choice_level::everywhere) { return true; }, [](const auto&) { return false; }}, level);
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      parts.fallback.apply({.width = 64.0f, .alignSelf = scene::align::kMiddle});
      parts.fallback.setVisible(!everywhere);
      parts.choices.reserve(kChoices.size());
      for (const std::int64_t most : kChoices) {
        parts.choices.emplace_back(label_of(most), choose{this, most});
        parts.choices.back().apply({.width = 64.0f, .alignSelf = scene::align::kMiddle});
      }
      this->show_choice(everywhere ? std::optional<std::int64_t>(now.value_or(5000)) : now);
    }
    void show_choice(std::optional<std::int64_t> now) {
      parts.fallback.set_active(!now);
      for (std::size_t i = 0; i < kChoices.size(); ++i)
        parts.choices[i].set_active(now == kChoices[i]);
    }
    void chose(std::optional<std::int64_t> most) {
      this->show_choice(most);
      actions->set_jump_search(level, most);
    }
  };
  // Made where it stays, apart from the page: its switches know it by its
  // address.
  struct parts_t {
    std::vector<row> rows;
  } parts;
  jump_search_choice(Actions* a, choice_level_t at, std::optional<std::int64_t> now) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(1);
    parts.rows.emplace_back(a, at, now);
  }
};

// Whether link previews show, at one level: Show and Hide and, where a
// level under decides for it, Default.
template <class Actions>
struct previews_choice : nodes::Stack {
  struct row;
  struct choose {
    row* in = nullptr;
    std::optional<bool> show;
    void operator()() const { in->chose(show); }
  };
  struct row : nodes::Stack {
    Actions* actions = nullptr;
    choice_level_t level;
    struct parts_t {
      nodes::Text label;
      segment<choose> fallback, show, hide;
    } parts;
    row(Actions* a, choice_level_t at, std::optional<bool> now)
        : actions(a), level(at),
          parts{.label = nodes::Text("Link previews", 14.0f, text_colour),
                .fallback = segment<choose>("Default", {this, std::nullopt}),
                .show = segment<choose>("Show", {this, true}),
                .hide = segment<choose>("Hide", {this, false})} {
      const bool everywhere =
          splice::visit(splice::overloaded{[](choice_level::everywhere) { return true; }, [](const auto&) { return false; }}, level);
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (segment<choose>* each : {&parts.fallback, &parts.show, &parts.hide})
        each->apply({.width = 70.0f, .alignSelf = scene::align::kMiddle});
      parts.fallback.setVisible(!everywhere);
      this->show_choice(everywhere ? std::optional<bool>(now.value_or(true)) : now);
    }
    void show_choice(std::optional<bool> now) {
      parts.fallback.set_active(!now);
      parts.show.set_active(now == true);
      parts.hide.set_active(now == false);
    }
    void chose(std::optional<bool> now) {
      this->show_choice(now);
      actions->set_link_previews(level, now);
    }
  };
  struct parts_t {
    std::vector<row> rows;
  } parts;
  previews_choice(Actions* a, choice_level_t at, std::optional<bool> now) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(1);
    parts.rows.emplace_back(a, at, now);
  }
};

// Whether read receipts show as faces, at one level: Show and Hide and,
// where a level under decides for it, Default -- as a room event kind's row.
template <class Actions>
struct receipts_choice : nodes::Stack {
  struct row;
  struct choose {
    row* in = nullptr;
    std::optional<bool> show;
    void operator()() const { in->chose(show); }
  };
  struct row : nodes::Stack {
  Actions* actions = nullptr;
  choice_level_t level;
  struct parts_t {
    nodes::Text label;
    segment<choose> fallback, show, hide;
  } parts;
  row(Actions* a, choice_level_t at, std::optional<bool> now)
      : actions(a), level(at),
        parts{.label = nodes::Text("Read receipts as faces", 14.0f, text_colour),
              .fallback = segment<choose>("Default", {this, std::nullopt}),
              .show = segment<choose>("Show", {this, true}),
              .hide = segment<choose>("Hide", {this, false})} {
    const bool everywhere =
        splice::visit(splice::overloaded{[](choice_level::everywhere) { return true; }, [](const auto&) { return false; }}, level);
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    parts.label.setElided(true);
    parts.label.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    for (segment<choose>* each : {&parts.fallback, &parts.show, &parts.hide})
      each->apply({.width = 70.0f, .alignSelf = scene::align::kMiddle});
    parts.fallback.setVisible(!everywhere);
    this->show_choice(everywhere ? std::optional<bool>(now.value_or(false)) : now);
  }
  void show_choice(std::optional<bool> now) {
    parts.fallback.set_active(!now);
    parts.show.set_active(now == true);
    parts.hide.set_active(now == false);
  }
  void chose(std::optional<bool> now) {
    this->show_choice(now);
    actions->set_receipts_shown(level, now);
  }
  };
  // Made where it stays, apart from the page: its switches know it by its
  // address, as event_kind_list's rows.
  struct parts_t {
    std::vector<row> rows;
  } parts;
  receipts_choice(Actions* a, choice_level_t at, std::optional<bool> now) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(1);
    parts.rows.emplace_back(a, at, now);
  }
};

// A notification as mux shows it itself, as Telegram Desktop's own: a card
// in a small window of its own -- the chat's avatar beside the title over
// the text.
struct toast_card : nodes::Stack {
  struct parts_t {
    avatar_mark face;
    two_lines texts;
  } parts;
  toast_card(std::string key, std::string title, std::string text)
      : parts{.face = avatar_mark(key, title, 44.0f), .texts = two_lines(title, std::move(text), 14.0f, 4.0f)} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fill = true, .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .background = sidebar_colour,
                  .border = scene::Border{band_colour, 1.0f}});
  }
};

}  // namespace mux::ui
