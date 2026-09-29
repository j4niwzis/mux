// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:controls -- The small controls: marks, rows, icon buttons, headers, segments, the drawer button.
export module mux.ui:controls;

import std;
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
    mark.setVisible(std::visit([](auto one) { return drawn(one); }, icon));
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

// Which room events show, at one level -- every account's, one's, a chat's:
// a row for all of them, then one for each kind, each with Show and Hide
// and, where a level under decides for it, Default. One node for every
// level; a choice goes to the program as it is made.
template <class Actions>
struct event_kind_list : nodes::Stack {
  struct row;
  struct choose {
    row* in = nullptr;
    std::optional<bool> show;
    void operator()() const { in->chose(show); }
  };
  struct row : nodes::Stack {
    Actions* actions = nullptr;
    choice_level_t level;
    std::optional<room_event_t> kind;
    struct parts_t {
      nodes::Text label;
      segment<choose> fallback, show, hide;
    } parts;
    row(Actions* a, choice_level_t at, std::optional<room_event_t> which, std::string_view text,
        std::optional<bool> now, bool with_default)
        : actions(a), level(at), kind(which),
          parts{.label = nodes::Text(std::string(text), 14.0f, text_colour),
                .fallback = segment<choose>("Default", {this, std::nullopt}),
                .show = segment<choose>("Show", {this, true}),
                .hide = segment<choose>("Hide", {this, false})} {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
      parts.label.setElided(true);
      parts.label.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (segment<choose>* each : {&parts.fallback, &parts.show, &parts.hide})
        each->apply({.width = 70.0f, .alignSelf = scene::align::kMiddle});
      parts.fallback.setVisible(with_default);
      this->show_choice(with_default ? now : std::optional<bool>(now.value_or(true)));
    }
    void show_choice(std::optional<bool> now) {
      parts.fallback.set_active(!now);
      parts.show.set_active(now == true);
      parts.hide.set_active(now == false);
    }
    void chose(std::optional<bool> now) {
      this->show_choice(now);
      actions->set_room_event_kind(level, kind, now);
    }
  };
  struct parts_t {
    std::vector<row> rows;
  } parts;
  event_kind_list(Actions* a, choice_level_t level, std::optional<bool> all,
                  const std::optional<config::room_event_kinds>& kinds) {
    const bool everywhere =
        std::visit(overloaded{[](choice_level::everywhere) { return true; }, [](const auto&) { return false; }}, level);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    // Made where they stay: each row's switches know it by its address.
    parts.rows.reserve(1 + kRoomEventKinds);
    parts.rows.emplace_back(a, level, std::nullopt, "All room events", all, !everywhere);
    for (const room_event_t& kind : all_room_events)
      parts.rows.emplace_back(a, level, kind, std::visit([](auto one) { return label_of(one); }, kind),
                              logic::choice_of(kinds, kind), true);
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
        std::visit(overloaded{[](choice_level::everywhere) { return true; }, [](const auto&) { return false; }}, level);
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
