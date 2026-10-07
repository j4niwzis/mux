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
import skiff.widgets.button;
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
  explicit icon_mark(const palette& colours, icon_t mark = icon::none{}) : nodes::Icon(shape_of(mark), colours.dim) {
    fState.apply({.width = 28.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle});
  }
};
// A radio's ring, with a dot in it while it is the one chosen.
struct radio_mark : nodes::Icon {
  bool on = false;
  // The colours it is lit in as it is chosen.
  const palette* colours_ = nullptr;
  explicit radio_mark(const palette& colours) : nodes::Icon(shape(false), colours.dim), colours_(&colours) {
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
    this->setColour(chosen ? colours_->accent : colours_->dim);
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
  two_lines(const palette& colours, std::string first, std::string second, float size, float gap)
      : two_lines(colours, std::move(first), std::move(second), size, gap, size - 2.0f) {}
  // The line under it its own size: an account's facts under its name.
  two_lines(const palette& colours, std::string first, std::string second, float size, float gap, float second_size)
      : parts{.name = nodes::Text(std::move(first), size, colours.text, true),
              .state = nodes::Text(std::move(second), second_size, colours.dim)} {
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
struct row_item : pressable<nodes::Stack> {
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
  row_item(const palette& colours, std::string text, Act what, icon_t icon = icon::none{}, std::optional<bool> choice = std::nullopt)
      : act(std::move(what)), radio(choice),
        parts{.mark = icon_mark(colours, icon), .label = nodes::Text(std::move(text), 15.0f, colours.text), .dot = radio_mark(colours)} {
    auto& [mark, label, dot] = parts;
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}, .hoverBackground = colours.chosen, .selectedBackground = colours.chosen, .focusBackground = colours.chosen});
    mark.setVisible(spl::visit([](auto one) { return drawn(one); }, icon));
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

  [[nodiscard]] bool focusChangesAppearance() const { return true; }
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
  icon_button(const palette& colours, icon_t mark, Act what)
      : act(std::move(what)), parts{.mark = nodes::Icon(shape_of(mark), colours.text)} {
    fState.apply({.width = 36.0f,
                  .height = 36.0f,
                  .cornerRadius = 18.0f,
                  .hoverBackground = colours.chosen,
                  .focusBackground = colours.chosen});
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

// A page with nowhere to go back to: its header's ← hidden, and pressing
// it does nothing.
struct no_action {
  void operator()() const {}
};
using no_back = no_action;

// The head of a page: ← on the left where there is somewhere to go back to,
// the page's name, and ✕ on the right where the page closes. Every panel,
// box and page that has a title and a ✕ has this one.
template <class Back, class Close>
struct page_header : nodes::Stack {
  struct parts_t {
    icon_button<Back> back;
    nodes::Text title;
    icon_button<Close> close;
  } parts;

  static constexpr float kHeight = 54.0f;

  page_header(const palette& colours, std::string name, Back to, Close shut, bool has_back, bool has_close)
      : parts{.back = icon_button<Back>(colours, icon::back{}, std::move(to)),
              .title = nodes::Text(std::move(name), 17.0f, colours.text, true),
              .close = icon_button<Close>(colours, icon::close{}, std::move(shut))} {
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
  // Esc, as its ← is pressed: a step back where it has one -- false where
  // it has none, for what holds it to close instead.
  bool step_back() {
    if (!parts.back.visible())
      return false;
    parts.back.act();
    return true;
  }
};

// One segment of a segmented control: square, its text centred, filled
// with the accent while it is the one chosen.
template <class Act>
struct segment : pressable<nodes::Stack> {
  Act act;
  bool active = false;
  struct parts_t {
    nodes::Text label;
  } parts;

  // The colours its text turns as it is chosen.
  const palette* colours_ = nullptr;
  segment(const palette& colours, std::string text, Act what)
      : act(std::move(what)), parts{.label = nodes::Text(std::move(text), 13.0f, colours.text, true)}, colours_(&colours) {
    fState.apply({.width = 92.0f, .height = 28.0f, .hoverBackground = colours.chosen, .selectedBackground = colours.accent, .focusBackground = colours.chosen});
    fStack.justify = nodes::justify::middle{};
    parts.label.apply({.alignSelf = scene::align::kMiddle});
  }

  void set_active(bool on) {
    active = on;
    parts.label.setColour(on ? colours_->on_accent : colours_->text);
    fState.apply({.selected = on});
  }

  [[nodiscard]] bool focusChangesAppearance() const { return true; }
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
  } parts;
  Actions* actions = nullptr;

  menu_button(const palette& colours, Actions* a)
      : parts{.bars = nodes::Icon(IconShape{{{nodes::mark::rect{-8.0f, -7.0f, 8.0f, -5.0f, 1.0f}, 0.0f, true},
                                             {nodes::mark::rect{-8.0f, -1.0f, 8.0f, 1.0f, 1.0f}, 0.0f, true},
                                             {nodes::mark::rect{-8.0f, 5.0f, 8.0f, 7.0f, 1.0f}, 0.0f, true}}},
                                  colours.text)},
        actions(a) {
    fState.apply({.width = 36.0f, .height = 36.0f, .cornerRadius = 8.0f, .hoverBackground = colours.chosen,
                  .focusBackground = colours.chosen});
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
[[nodiscard]] constexpr std::string_view label_of(room_event::unreadable) { return "Events mux cannot read"; }
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
    head_t(const palette& colours, std::string label, std::string value)
        : parts{.label = nodes::Text(std::move(label), 13.0f, colours.dim),
                .value = nodes::Text(std::move(value), 14.0f, colours.text),
                .chevron = nodes::Icon(shape_of(icon::down{}), colours.dim)} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f,
                    .background = colours.tile, .hoverBackground = colours.chosen, .border = scene::Border{colours.band, 1.0f}});
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
    option_t(const palette& colours, std::string name, bool chosen)
        : parts{.name = nodes::Text(std::move(name), 14.0f, chosen ? colours.accent : colours.text, chosen)} {
      this->setHorizontal();
      fState.apply({.fillX = true, .height = 32.0f, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f,
                    .hoverBackground = colours.chosen});
      parts.name.apply({.alignSelf = scene::align::kMiddle});
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  };
  struct parts_t {
    head_t head;
    std::vector<option_t> options;
  } parts;
  choice_menu(const palette& colours, std::string label, const std::vector<std::string>& names, std::size_t current, Choose c)
      : choose(std::move(c)), parts{.head = head_t(colours, std::move(label), current < names.size() ? names[current] : std::string())} {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.options.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
      parts.options.emplace_back(colours, names[i], i == current);
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
    row(Actions* a, const palette& colours, const std::string& account, const space_item_shown& one)
        : parts{.name = nodes::Text(one.name, 14.0f, colours.text),
                .where = choice_menu<pick_bars>(colours, "", {"Side bar", "Top bar", "Both bars", "Hidden"},
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
  spaces_choices(Actions* a, const palette& colours, const ui_shared& shared) {
    this->setGap(2.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(shared.space_items.size());
    for (const space_item_shown& one : shared.space_items)
      parts.rows.emplace_back(a, colours, shared.space_account, one);
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
  return spl::visit(spl::overloaded{[](choice_level::everywhere) -> room_events_held& { return everywhere; },
                                          [](choice_level::account) -> room_events_held& { return account; },
                                          [](choice_level::chat) -> room_events_held& { return chat; }},
                       level);
}
// What is shown with a level as it is, and the levels over it.
[[nodiscard]] inline room_event_filter events_in_effect(const choice_level_t& level) {
  const room_events_held& every = room_events_at(choice_level::everywhere{});
  const room_events_held& account = room_events_at(choice_level::account{});
  const room_events_held& chat = room_events_at(choice_level::chat{});
  return spl::visit(
      spl::overloaded{[&](choice_level::everywhere) {
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
  return spl::visit(spl::overloaded{[](choice_level::chat) { return events_in_effect(choice_level::account{}); },
                                          [](const auto&) { return events_in_effect(choice_level::everywhere{}); }},
                       level);
}

// Which room events show, at one level -- every account's, one's, a chat's:
// how, as a dropdown -- As above (under the top), All events, Messages only,
// or Custom -- and under it a row for each kind, Show or Hide. The rows say
// what is in effect however the level is set; they are chosen only where
// it is Custom, and greyed where not: nothing under a choice that overrides
// it looks as if it did something. Custom starts from what was in effect.
// A setting's row at a level: its label at the left, cut short where the
// room runs out, and its choices at the right.
inline void lay_out_setting_row(nodes::Stack& row, nodes::Text& label) {
  row.setHorizontal();
  row.setGap(4.0f);
  row.fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
  label.setElided(true);
  label.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
}

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
    row(Actions* a, const palette& colours, choice_level_t at, room_event_t which, std::string_view text)
        : actions(a), level(at), kind(which),
          parts{.label = nodes::Text(std::string(text), 14.0f, colours.text),
                .show = segment<choose>(colours, "Show", {this, true}),
                .hide = segment<choose>(colours, "Hide", {this, false})} {
      lay_out_setting_row(*this, parts.label);
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
  [[nodiscard]] static std::size_t way_of(const choice_level_t& level, std::optional<bool> all,
                                          const std::optional<config::room_event_kinds>& kinds) {
    const bool custom = kinds && std::ranges::any_of(all_room_events, [&](const room_event_t& kind) {
                          return logic::choice_of(kinds, kind).has_value();
                        });
    if (custom)
      return kCustom;
    if (!all)
      return has_level_above(level) ? kAbove : kAll;
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
      const std::size_t way = index + (has_level_above(level) ? 0 : 1);
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
  event_kind_list(Actions* a, const palette& colours, choice_level_t level, std::optional<bool> all,
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
      parts.rows.emplace_back(a, colours, level, kind, spl::visit([](auto one) { return label_of(one); }, kind));
      parts.rows.back().show_value(shown.shows(kind));
      parts.rows.back().set_live(way == kCustom);
    }
    std::vector<std::string> names;
    if (has_level_above(level))
      names.emplace_back("As above");
    for (const char* name : {"All events", "Messages only", "Custom"})
      names.emplace_back(name);
    parts.way.emplace(colours, "Room events", names, way - (has_level_above(level) ? 0 : 1),
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
    row(Actions* a, const palette& colours, choice_level_t at, std::optional<std::int64_t> now)
        : actions(a), level(at),
          parts{.label = nodes::Text("Look back for a message", 14.0f, colours.text),
                .fallback = segment<choose>(colours, "Default", {this, std::nullopt})} {
      const bool everywhere = !has_level_above(level);
      lay_out_setting_row(*this, parts.label);
      parts.fallback.apply({.width = 64.0f, .alignSelf = scene::align::kMiddle});
      parts.fallback.setVisible(!everywhere);
      parts.choices.reserve(kChoices.size());
      for (const std::int64_t most : kChoices) {
        parts.choices.emplace_back(colours, label_of(most), choose{this, most});
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
  jump_search_choice(Actions* a, const palette& colours, choice_level_t at, std::optional<std::int64_t> now) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(1);
    parts.rows.emplace_back(a, colours, at, now);
  }
};

// A thing on or off, at one level: its two words (Show and Hide, Send and
// Don't) and, where a level under decides for it, Default -- as a room event
// kind's row. What it is says its label and words, what it is everywhere
// when nothing was said, and how it is set: link previews, read receipts as
// faces, typing notifications.
template <class Actions, class Setting>
struct show_hide_choice : nodes::Stack {
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
    row(Actions* a, const palette& colours, choice_level_t at, std::optional<bool> now)
        : actions(a), level(at),
          parts{.label = nodes::Text(std::string(Setting::label), 14.0f, colours.text),
                .fallback = segment<choose>(colours, "Default", {this, std::nullopt}),
                .show = segment<choose>(colours, std::string(Setting::yes), {this, true}),
                .hide = segment<choose>(colours, std::string(Setting::no), {this, false})} {
      const bool everywhere = !has_level_above(level);
      lay_out_setting_row(*this, parts.label);
      for (segment<choose>* each : {&parts.fallback, &parts.show, &parts.hide})
        each->apply({.width = 70.0f, .alignSelf = scene::align::kMiddle});
      parts.fallback.setVisible(!everywhere);
      this->show_choice(everywhere ? std::optional<bool>(now.value_or(Setting::unsaid)) : now);
    }
    void show_choice(std::optional<bool> now) {
      parts.fallback.set_active(!now);
      parts.show.set_active(now == true);
      parts.hide.set_active(now == false);
    }
    void chose(std::optional<bool> now) {
      this->show_choice(now);
      Setting::set(*actions, level, now);
    }
  };
  // Made where it stays, apart from the page: its switches know it by its
  // address, as event_kind_list's rows.
  struct parts_t {
    std::vector<row> rows;
  } parts;
  show_hide_choice(Actions* a, const palette& colours, choice_level_t at, std::optional<bool> now) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.rows.reserve(1);
    parts.rows.emplace_back(a, colours, at, now);
  }
  // Shown again as it is now: said at every chat's level, whatever was saved.
  void show(std::optional<bool> now) {
    auto& one = parts.rows.front();
    one.show_choice(has_level_above(one.level) ? now : std::optional<bool>(now.value_or(Setting::unsaid)));
  }
};

// Notifications, the same rows at every level: on, of mentions alone, the
// sender's name, the text, a sound -- each chosen through set_notify_choice.
template <class Which>
struct notify_setting_base {
  static constexpr bool unsaid = Which::unsaid;
  template <class Actions>
  static void set(Actions& actions, choice_level_t level, std::optional<bool> now) {
    actions.set_notify_choice(level, config::notify_setting_t{Which{}}, now);
  }
};
struct notify_on_setting : notify_setting_base<config::notify_setting::on> {
  static constexpr std::string_view label = "Notifications";
  static constexpr std::string_view yes = "On", no = "Off";
};
struct notify_mentions_setting : notify_setting_base<config::notify_setting::mentions> {
  static constexpr std::string_view label = "Of messages";
  static constexpr std::string_view yes = "Mentions", no = "All";
};
struct notify_name_setting : notify_setting_base<config::notify_setting::name> {
  static constexpr std::string_view label = "The sender's name";
  static constexpr std::string_view yes = "Show", no = "Hide";
};
struct notify_text_setting : notify_setting_base<config::notify_setting::text> {
  static constexpr std::string_view label = "The message's text";
  static constexpr std::string_view yes = "Show", no = "Hide";
};
struct notify_sound_setting : notify_setting_base<config::notify_setting::sound> {
  static constexpr std::string_view label = "Sound";
  static constexpr std::string_view yes = "Play", no = "Silent";
};
template <class Actions>
struct notify_choice_rows : nodes::Stack {
  struct parts_t {
    show_hide_choice<Actions, notify_on_setting> on;
    show_hide_choice<Actions, notify_mentions_setting> mentions;
    show_hide_choice<Actions, notify_name_setting> name;
    show_hide_choice<Actions, notify_text_setting> text;
    show_hide_choice<Actions, notify_sound_setting> sound;
  } parts;
  notify_choice_rows(Actions* a, const palette& colours, choice_level_t level, const config::notify_choices& now, float gap = 8.0f)
      : parts{.on = {a, colours, level, now.on},
              .mentions = {a, colours, level, now.mentions},
              .name = {a, colours, level, now.name},
              .text = {a, colours, level, now.text},
              .sound = {a, colours, level, now.sound}} {
    this->setGap(gap);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  void show(const config::notify_choices& now) {
    auto& [on, mentions, name, text, sound] = parts;
    on.show(now.on);
    mentions.show(now.mentions);
    name.show(now.name);
    text.show(now.text);
    sound.show(now.sound);
  }
};

// Link previews: shown, where nothing says otherwise.
struct link_previews_setting {
  static constexpr std::string_view label = "Link previews";
  static constexpr std::string_view yes = "Show", no = "Hide";
  static constexpr bool unsaid = true;
  template <class Actions>
  static void set(Actions& actions, choice_level_t level, std::optional<bool> now) {
    actions.set_link_previews(level, now);
  }
};
// Read receipts as faces: not, where nothing says otherwise.
struct receipts_setting {
  static constexpr std::string_view label = "Read receipts as faces";
  static constexpr std::string_view yes = "Show", no = "Hide";
  static constexpr bool unsaid = false;
  template <class Actions>
  static void set(Actions& actions, choice_level_t level, std::optional<bool> now) {
    actions.set_receipts_shown(level, now);
  }
};
// Link previews fetched from the sites themselves, through the account's
// proxy, or through its server: the server, where nothing says
// otherwise.
struct previews_direct_setting {
  static constexpr std::string_view label = "Fetch link previews";
  static constexpr std::string_view yes = "From site", no = "Server";
  static constexpr bool unsaid = false;
  template <class Actions>
  static void set(Actions& actions, choice_level_t level, std::optional<bool> now) {
    actions.set_previews_direct(level, now);
  }
};
template <class Actions>
using previews_direct_choice = show_hide_choice<Actions, previews_direct_setting>;
// Others told one is typing -- never what: sent, where nothing says
// otherwise.
struct typing_setting {
  static constexpr std::string_view label = "Send typing notifications";
  static constexpr std::string_view yes = "Send", no = "Don't";
  static constexpr bool unsaid = true;
  template <class Actions>
  static void set(Actions& actions, choice_level_t level, std::optional<bool> now) {
    actions.set_typing_sent(level, now);
  }
};
template <class Actions>
using typing_choice = show_hide_choice<Actions, typing_setting>;
template <class Actions>
using previews_choice = show_hide_choice<Actions, link_previews_setting>;
template <class Actions>
using receipts_choice = show_hide_choice<Actions, receipts_setting>;

// What a chat shows, at a level -- its room events, read receipts as faces,
// link previews (and in direct messages), how far a jump looks back -- as
// in effect there: the same rows wherever they are chosen (every chat's in
// Storage, an account's, one room's).
struct chat_choice_values {
  std::optional<bool> events_all;
  std::optional<config::room_event_kinds> event_kinds;
  std::optional<bool> receipts;
  std::optional<bool> previews;
  std::optional<bool> previews_direct;
  std::optional<std::int64_t> jump_search;
};
template <class Actions>
struct chat_choices : nodes::Stack {
  struct parts_t {
    event_kind_list<Actions> events;
    receipts_choice<Actions> receipts;
    previews_choice<Actions> previews;
    previews_direct_choice<Actions> previews_direct;
    jump_search_choice<Actions> jump_search;
  } parts;
  // Spaced as the page it is in spaces its rows.
  chat_choices(Actions* a, const palette& colours, choice_level_t level, const chat_choice_values& now, float gap)
      : parts{.events = event_kind_list<Actions>(a, colours, level, now.events_all, now.event_kinds),
              .receipts = receipts_choice<Actions>(a, colours, level, now.receipts),
              .previews = previews_choice<Actions>(a, colours, level, now.previews),
              .previews_direct = previews_direct_choice<Actions>(a, colours, level, now.previews_direct),
              .jump_search = jump_search_choice<Actions>(a, colours, level, now.jump_search)} {
    this->setGap(gap);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
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
  toast_card(const palette& colours, std::string key, std::string title, std::string text)
      : parts{.face = avatar_mark(key, title, 44.0f), .texts = two_lines(colours, title, std::move(text), 14.0f, 4.0f)} {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fill = true, .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .background = colours.sidebar,
                  .border = scene::Border{colours.band, 1.0f}});
  }
};

// What a message being written answers or edits, as shown over the field:
// its icon, its title ("Reply to <name>", "Edit message"), a line of it.
struct compose_context {
  icon_t mark;
  std::string title;
  std::string line;
};

// What is written answers or edits, as tdesktop's FieldHeader shows it:
// its icon in the left column (historyReplySkip wide), then two lines --
// "Reply to <name>" or "Edit message" in the accent, semibold, over a line
// of the message -- and ✕ on the right to go back to a plain one. One bar
// for every field that answers: the chat's composer, a thread's;
// Cancel is what its ✕ does there.
template <class Cancel>
struct context_bar : nodes::Stack {
  static constexpr float kHeight = 49.0f;  // historyReplyHeight
  static constexpr float kSkip = 51.0f;    // historyReplySkip
  struct lines_column : nodes::Stack {
    struct parts_t {
      nodes::Text title;
      nodes::Text line;
    } parts;
    explicit lines_column(const palette& colours)
        : parts{.title = nodes::Text("", 13.0f, colours.accent, true), .line = nodes::Text("", 13.0f, colours.text)} {
      this->setGap(2.0f);
      for (nodes::Text* each : {&parts.title, &parts.line}) {
        each->setElided(true);
        each->apply({.fillX = true});
      }
    }
  };
  using cancel_button = icon_button<Cancel>;
  struct parts_t {
    nodes::Icon mark;  // in the left column
    lines_column lines;
    cancel_button cancel;
  } parts;
  context_bar(const palette& colours, Cancel cancel)
      : parts{.mark = nodes::Icon(IconShape{}, colours.accent),
              .lines = lines_column(colours),
              .cancel = cancel_button(colours, icon::close{}, std::move(cancel))} {
    this->setHorizontal();
    this->setGap(8.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 8.0f, 0.0f, 0.0f}});
    parts.mark.apply({.fillY = true, .width = kSkip - 8.0f});
    parts.lines.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    parts.cancel.apply({.alignSelf = scene::align::kMiddle});
    this->setVisible(false);
  }
  // What is answered or edited, shown; or nothing, hidden.
  void show(std::optional<compose_context> said) {
    this->setVisible(said.has_value());
    parts.mark.setShape(said ? shape_of(said->mark) : IconShape{});
    parts.lines.parts.title.setText(said ? said->title : std::string());
    parts.lines.parts.line.setText(said ? said->line : std::string());
    this->markDamaged();
  }
};


// A count on an accent badge over the top of a round button: the @ and the
// heart's, and the down arrow's.
struct count_badge : nodes::Stack {
  struct parts_t {
    nodes::Text count;
  } parts;
  explicit count_badge(const palette& colours) : parts{.count = nodes::Text("", 11.0f, colours.on_accent, true)} {
    fState.apply({.place = scene::anchor::kTopCentre,
                  .y = -10.0f,
                  .height = 18.0f,
                  .autoSize = scene::axes::kX,
                  .minWidth = 20.0f,
                  .padding = {1.0f, 5.0f, 1.0f, 5.0f},
                  .cornerRadius = 9.0f,
                  .background = colours.accent});
    fStack.justify = nodes::justify::middle{};
    parts.count.apply({.alignSelf = scene::align::kMiddle});
  }
};

// A label, and a small button on its right that is there only where it
// does something: an element's look over a way back to the bubbles'.
template <class Act>
struct label_button_row : nodes::Stack {
  struct parts_t {
    nodes::Text label;
    widgets::Button<Act> reset;
  } parts;
  label_button_row(const palette& colours, std::string label, std::string button, Act act, bool shown)
      : parts{.label = nodes::Text(std::move(label), 13.0f, colours.text), .reset = widgets::Button<Act>(colours.widgets, std::move(button), std::move(act))} {
    this->setHorizontal();
    this->setGap(6.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    parts.reset.apply({.width = 96.0f, .height = 26.0f});
    parts.reset.setVisible(shown);
  }
};


// A run of things under a name, as the emoji and sticker panels list them:
// the name, dim, over its cells, wrapped as wide as it is. The cells are put
// in by what it is a section of.
template <class Cell>
struct cell_section : nodes::Stack {
  using cells_t = nodes::Flow<std::vector<Cell>>;
  struct parts_t {
    nodes::Text title;
    cells_t cells{{.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, {}};
  } parts;
  cell_section(const palette& colours, std::string name) : parts{.title = nodes::Text(std::move(name), 13.0f, colours.dim, true)} {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.title.apply({.margin = {10.0f, 0.0f, 6.0f, 7.0f}});
    parts.cells.apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  [[nodiscard]] std::vector<Cell>& each() { return std::get<0>(parts.cells.fChildren); }
};

// A panel's footer of tabs, one for each of its sections.
template <class Tab>
struct tab_strip : nodes::Stack {
  struct parts_t {
    std::vector<Tab> each;
  } parts;
};


// A dialog's buttons, at its bottom right: Cancel, and what it does --
// Send, Save, Create room -- the primary one.
template <class Cancel, class Confirm>
struct dialog_buttons : nodes::Stack {
  struct parts_t {
    widgets::Button<Cancel> cancel;
    widgets::Button<Confirm> confirm;
  } parts;
  dialog_buttons(const palette& colours, std::string confirm, Cancel cancel_it, Confirm confirm_it, float width = 96.0f)
      : parts{.cancel = widgets::Button<Cancel>(colours.widgets, "Cancel", std::move(cancel_it)),
              .confirm = widgets::Button<Confirm>(colours.widgets, std::move(confirm), std::move(confirm_it))} {
    this->setHorizontal();
    this->setGap(8.0f);
    fStack.justify = nodes::justify::end{};
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {8.0f, 0.0f, 0.0f, 0.0f}});
    parts.confirm.setPrimary(true);
    parts.cancel.apply({.width = width, .height = 36.0f});
    parts.confirm.apply({.width = width, .height = 36.0f});
  }
};


// A row's top line, as a chat's in the list: the name, bold, as long as it
// can be, and the time on the right.
struct name_time_line : nodes::Stack {
  struct parts_t {
    nodes::Text name;
    nodes::Text time;
  } parts;
  name_time_line(std::string name, std::string time, skia::SkColor name_colour, skia::SkColor time_colour, float time_size)
      : parts{.name = nodes::Text(std::move(name), 13.0f, name_colour, true),
              .time = nodes::Text(std::move(time), time_size, time_colour)} {
    this->setHorizontal();
    this->setGap(8.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.name.setElided(true);
    parts.name.apply({.grow = scene::axes::kX});
  }
};


// An accent's circle: its colour, a ring where it is the one in use; pressed,
// Choose is told it.
template <class Choose>
struct accent_circle : scene::Node {
  Choose choose;
  config::accent_t accent;
  bool chosen = false;
  // Its shade in the theme in use.
  skia::SkColor shade;
  struct parts_t {
    nodes::Box<> dot;
  } parts;
  accent_circle(Choose what, config::accent_t which, const config::theme_t& in)
      : choose(std::move(what)), accent(which), shade(colour_of(which, in)), parts{.dot = nodes::Box<>(shade)} {
    fState.apply({.width = 34.0f, .height = 34.0f, .cornerRadius = 17.0f});
    parts.dot.apply({.place = scene::anchor::kCentre, .width = 24.0f, .height = 24.0f, .cornerRadius = 12.0f});
  }
  // A ring in its shade while it is the one in use.
  void set_chosen(bool on) {
    chosen = on;
    fState.apply({.border = scene::Border{on ? shade : 0u, on ? 2.0f : 0.0f}});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    choose(accent);
    return true;
  }
};

// Telegram's eight accents in a row -- the theme's own first, where it is
// one to choose: the window's accent, an account's colour.
template <class Choose>
struct accent_circles : nodes::Stack {
  struct parts_t {
    std::vector<accent_circle<Choose>> circles;
  } parts;
  accent_circles(Choose choose, const config::theme_t& in, bool with_theme_own) {
    this->setHorizontal();
    this->setGap(4.0f);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}});
    if (with_theme_own)
      parts.circles.emplace_back(choose, config::accent::theme_own{}, in);
    for (const config::accent_t& one :
         {config::accent_t{config::accent::blue{}}, config::accent_t{config::accent::green{}},
          config::accent_t{config::accent::pink{}}, config::accent_t{config::accent::orange{}},
          config::accent_t{config::accent::purple{}}, config::accent_t{config::accent::red{}},
          config::accent_t{config::accent::grey{}}, config::accent_t{config::accent::gold{}}})
      parts.circles.emplace_back(choose, one, in);
  }
  void show_chosen(const config::accent_t& now) {
    for (auto& circle : parts.circles)
      circle.set_chosen(circle.accent == now);
  }
};

// A row longer than its room, moved sideways -- by a finger or a mouse
// dragged along it, flicked to glide on, or a sideways wheel -- and cut to
// its room. Up and down are left to the page round it: a gesture going more
// that way than along is not taken, nor is an upright wheel.
template <class Line>
struct side_scroll : nodes::Stack {
  struct parts_t {
    Line line;
  } parts;
  explicit side_scroll(Line line) : parts{.line = std::move(line)} {
    this->setHorizontal();
    fState.apply({.masking = true});
  }
  // How far it goes along: what of the line is past its room.
  [[nodiscard]] float most() const {
    return std::max(0.0f, parts.line.bounds().width() - fState.contentBox().width());
  }
  [[nodiscard]] float offset() const noexcept { return gesture.offset(); }
  // Where the line is put: at the gesture's offset, its ends held to.
  void place() {
    const float at = scene::snapToPixel(gesture.offset());
    if (at == shown)
      return;
    shown = at;
    parts.line.apply({.shiftX = -at});
    this->markDamaged();
  }
  void scroll_by(float delta) {
    gesture.setBounds(0.0f, this->most());
    gesture.glideTo(gesture.target() + delta);
    scene::work::mark(fState.fId);
  }

  using Node::onPointer;
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::scroll& wheel, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::bubble> || std::same_as<Phase, scene::phase::target>)
  {
    if (wheel.dx == 0.0f || this->most() <= 0.0f)
      return;
    this->scroll_by(-wheel.dx * 40.0f);
    reply.handle();
  }
  // The press is only noted, and what is under it clicked later: a press
  // that goes along is a drag of the row, not a choice.
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::down& press, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    if (press.button > 1 || this->most() <= 0.0f)
      return;
    gesture.setBounds(0.0f, this->most());
    armed = true;
    press_x = press.x;
    press_y = press.y;
    if constexpr (std::same_as<Phase, scene::phase::capture>)
      reply.deferClick();
    if (gesture.press(press.x))
      reply.handle();  // the press was spent catching a glide
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::move& move, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    if (!armed)
      return;
    if (!gesture.dragging()) {
      const float dx = move.x - press_x;
      const float dy = move.y - press_y;
      // Going more up or down than along: the page's, from here on.
      if (std::abs(dy) >= scene::ScrollGesture::kSlop && std::abs(dy) > std::abs(dx)) {
        armed = false;
        return;
      }
    }
    const bool was = gesture.dragging();
    if (!gesture.drag(move.x, now_ms()))
      return;
    if (!was) {
      if (reply.fCaptured) {
        armed = false;  // something else is dragged already
        return;
      }
      reply.capturePointer();
    }
    reply.suppressHover();
    this->place();
    reply.handle();
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::up&, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    this->finish(reply);
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::cancel&, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    this->finish(reply);
  }
  void finish(scene::PointerReply& reply) {
    armed = false;
    if (!gesture.dragging())
      return;
    gesture.release();
    scene::work::mark(fState.fId);  // to glide on, or spring back
    reply.releasePointer();
    reply.suppressHover();
    reply.handle();
  }
  [[nodiscard]] bool acceptsInput() const { return true; }

  // Gliding: a frame at a time, until it rests.
  void update(double now) {
    const double dt = last_ms > 0.0 ? now - last_ms : 16.0;
    last_ms = now;
    gesture.setBounds(0.0f, this->most());
    if (gesture.advance(dt))
      this->place();
  }
  [[nodiscard]] bool wantsTick() const { return gesture.moving() || gesture.dragging(); }

 private:
  [[nodiscard]] static double now_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  scene::ScrollGesture gesture;
  float shown = 0.0f;
  bool armed = false;
  float press_x = 0.0f, press_y = 0.0f;
  double last_ms = 0.0;
};


}  // namespace mux::ui
