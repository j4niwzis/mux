// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:controls -- The small controls: marks, rows, icon buttons, headers, segments, the drawer button.
export module mux.ui:controls;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.text;
import skiff.widgets.avatar;
import skiff.widgets.button;
import skiff.widgets.model;
import skiff.widgets.sliderbar;
import skiff.model;
import skiff.bind;
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
inline auto icon_mark(const palette& colours, icon_t mark = icon::none{}, float width = 28.0f, float height = 36.0f) {
  return skiff::compose::styled({.width = width, .height = height, .alignSelf = scene::align::kMiddle},
                                nodes::Icon(shape_of(mark), colours.dim));
}
using icon_mark_t = decltype(icon_mark(std::declval<const palette&>()));
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
  using Answer = std::variant<::skiff::scene::Taken, ::mux::ui::request::open_avatar>;
  avatar_button(std::string id, std::string shown, float size)
      : avatar_mark(std::move(id), shown, size) {
    fState.setCursor(scene::cursor::hand{});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  std::optional<Answer> onClick(float, float) {
    if (key.empty())
      return ::skiff::scene::Taken{};
    return ::mux::ui::request::open_avatar{key};
  }
};

// A name over how it is: two lines, each cut where it runs out of room,
// taking what their row leaves them.
struct two_line_style {
  float second_size = 0.0f;
  std::optional<skia::SkColor> first_ink;
  std::optional<skia::SkColor> second_ink;
  bool selectable = false;
  std::optional<bool> show_second;
};
inline auto two_lines(const palette& colours, std::string first, std::string second,
                      float size, float gap, two_line_style style = {}) {
  return skiff::compose::column(
      skiff::compose::vbox(gap, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
      skiff::compose::styled({.fillX = true}, elided(nodes::Text(std::move(first), size,
          style.first_ink.value_or(colours.text), true, style.selectable))),
      skiff::compose::visible(style.show_second.value_or(!second.empty()),
          skiff::compose::styled({.fillX = true}, elided(nodes::Text(second,
              style.second_size > 0.0f ? style.second_size : size - 2.0f,
              style.second_ink.value_or(colours.dim), false, style.selectable)))));
}
inline auto two_lines(const palette& colours, std::string first, std::string second,
                      float size, float gap, float second_size) {
  return two_lines(colours, std::move(first), std::move(second), size, gap, two_line_style{.second_size = second_size});
}
using two_lines_t = decltype(two_lines(std::declval<const palette&>(), "", "", 14.0f, 4.0f));

struct row_look {
  float height = 46.0f, gap = 16.0f;
  scene::Margin padding{0.0f, 20.0f, 0.0f, 20.0f};
  float icon_width = 28.0f, icon_height = 36.0f, text_size = 15.0f;
};
inline constexpr row_look menu_row_look{
    33.0f, 15.0f, {0.0f, 17.0f, 0.0f, 15.0f}, 24.0f, 24.0f, 13.0f};

template <class Act>
struct row_item : pressable<skiff::compose::Stacked> {
  Act act;
  // Whether it is one of a choice, and the chosen one.
  std::optional<bool> radio;
  struct parts_t {
    icon_mark_t mark;
    nodes::Text label;
    radio_mark dot;
  } parts;

  static constexpr float kHeight = 46.0f;

  // Declared: its icon, its text taking the room, and a radio at the end.
  row_item(const palette &colours, std::string text, Act what,
           icon_t icon = icon::none{},
           std::optional<bool> choice = std::nullopt, row_look look = {})
      : pressable<skiff::compose::Stacked>(skiff::compose::hbox(
            look.gap, {.fillX = true,
                       .height = look.height,
                       .padding = look.padding,
                       .hoverBackground = colours.chosen,
                       .selectedBackground = colours.chosen,
                       .focusBackground = colours.chosen})),
        act(std::move(what)), radio(choice),
        parts{.mark = skiff::compose::visible(
                  spl::visit([](auto one) { return drawn(one); }, icon),
                  icon_mark(colours, icon, look.icon_width, look.icon_height)),
              .label = skiff::compose::styled(
                  {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                  elided(nodes::Text(std::move(text), look.text_size,
                                     colours.text))),
              .dot = skiff::compose::visible(
                  choice.has_value(),
                  dot_lit(radio_mark(colours), choice.value_or(false)))} {}
  [[nodiscard]] static radio_mark dot_lit(radio_mark dot, bool on) {
    dot.set_on(on);
    return dot;
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
struct icon_button : skiff::compose::Specced {
  Act act;
  struct parts_t {
    nodes::Icon mark;
  } parts;

  // Round, lit under the pointer or the keyboard's focus.
  icon_button(const palette& colours, icon_t mark, Act what)
      : Specced({.width = 36.0f, .height = 36.0f, .cornerRadius = 18.0f, .hoverBackground = colours.chosen, .focusBackground = colours.chosen}),
        act(std::move(what)),
        parts{.mark = skiff::compose::styled({.fill = true}, nodes::Icon(shape_of(mark), colours.text))} {}
  void set_colour(skia::SkColor colour) { parts.mark.setColour(colour); }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act_on(fState, act);
    return true;
  }
  // Its act's answer, as the press is delivered.
  auto onPress()
    requires skiff::scene::Answering<Act>
  {
    return act();
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
inline constexpr float kPageHeaderHeight = 54.0f;
template <class Back, class Close>
auto page_header(const palette& colours, std::string name, Back back, Close close, bool has_back, bool has_close) {
  return skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      skiff::compose::visible(has_back, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button<Back>(colours, icon::back{}, std::move(back)))),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                             .margin = {0.0f, 0.0f, 0.0f, has_back ? 0.0f : 10.0f}},
          elided(nodes::Text(std::move(name), 17.0f, colours.text, true))),
      skiff::compose::visible(has_close, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button<Close>(colours, icon::close{}, std::move(close)))));
}
template <class Back, class Close>
using page_header_t = decltype(page_header(std::declval<const palette&>(), std::string{}, Back{}, Close{}, false, false));
// Esc activates the back control through the same press route as a click.
template <class Header> bool page_step_back(Header& header) {
  auto& back = std::get<0>(header.fParts);
  if (!back.visible())
    return false;
  (void)back.onClick(0.0f, 0.0f);
  return true;
}

// One segment of a segmented control: square, its text centred, filled
// with the accent while it is the one chosen.
template <class Act>
struct segment : pressable<skiff::compose::Stacked> {
  Act act;
  bool active = false;
  struct parts_t {
    nodes::Text label;
  } parts;

  // The colours its text turns as it is chosen.
  const palette* colours_ = nullptr;
  segment(const palette& colours, std::string text, Act what)
      : pressable<skiff::compose::Stacked>(skiff::compose::justified(
            skiff::compose::vbox(0.0f, {.width = 92.0f, .height = 28.0f, .hoverBackground = colours.chosen, .selectedBackground = colours.accent,
                                        .focusBackground = colours.chosen}),
            nodes::justify::middle{})),
        act(std::move(what)),
        parts{.label = skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(text), 13.0f, colours.text, true))},
        colours_(&colours) {}

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

// What a press of a field's part sets the field's own part to: given it as
// the field read the model -- nothing, where the part does nothing now.
template <class T>
struct sets {
  using Answer = std::optional<skiff::bind::Own<skiff::model::SetTo<T>>>;
  std::optional<T> next;
  Answer operator()() const {
    return next.transform([](const T& value) { return skiff::bind::own(skiff::model::setTo(value)); });
  }
};
// The same for one of a menu's options: what each sets, in their order.
template <class T>
struct sets_nth {
  using Answer = std::optional<skiff::bind::Own<skiff::model::SetTo<T>>>;
  std::vector<T> nexts;
  Answer operator()(std::size_t at) const {
    if (at >= nexts.size())
      return std::nullopt;
    return skiff::bind::own(skiff::model::setTo(nexts[at]));
  }
};

// ---- the drawer's button ------------------------------------------------------

// Three lines at the top-left of the conversation list: a press pulls the
// drawer out.
template <class Actions>
struct menu_button : skiff::compose::Specced {
  // What its handlers ask for, returned.
  using Answer = ::mux::ui::request::open_drawer;
  // Three bars, and a plate under them while it is hovered or focused.
  struct parts_t {
    nodes::Icon bars;
  } parts;

  menu_button(const palette& colours)
      : Specced({.width = 36.0f, .height = 36.0f, .cornerRadius = 8.0f, .hoverBackground = colours.chosen, .focusBackground = colours.chosen}),
        parts{.bars = skiff::compose::styled({.fill = true},
                                             nodes::Icon(IconShape{{{nodes::mark::rect{-8.0f, -7.0f, 8.0f, -5.0f, 1.0f}, 0.0f, true},
                                                                    {nodes::mark::rect{-8.0f, -1.0f, 8.0f, 1.0f, 1.0f}, 0.0f, true},
                                                                    {nodes::mark::rect{-8.0f, 5.0f, 8.0f, 7.0f, 1.0f}, 0.0f, true}}},
                                                         colours.text))} {}

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  std::optional<Answer> onClick(float, float) {
    return ::mux::ui::request::open_drawer{};
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

// The existing dropdown's head and option rows, built with combinators.
// Selection and pointer capture are shared with the widget library.
inline auto choice_menu_head(const palette& colours, std::string label, std::string value) {
  namespace c = skiff::compose;
  return c::row(c::hbox(8.0f, {.fillX = true, .height = 36.0f,
      .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f,
      .background = colours.tile, .hoverBackground = colours.chosen, .border = scene::Border{colours.band, 1.0f}}),
      c::visible(!label.empty(), c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(label, 13.0f, colours.dim)))),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(value), 14.0f, colours.text))),
      c::styled({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::down{}), colours.dim)));
}
inline auto choice_menu_option(const palette& colours, std::string name) {
  namespace c = skiff::compose;
  return c::row(c::hbox(0.0f, {.fillX = true, .height = 32.0f,
      .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f, .hoverBackground = colours.chosen}),
      c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(name), 14.0f, colours.text)));
}
using choice_menu_head_t = decltype(choice_menu_head(std::declval<const palette&>(), std::string(), std::string()));
using choice_menu_option_t = decltype(choice_menu_option(std::declval<const palette&>(), std::string()));
struct show_choice_menu {
  skia::SkColor text, accent;
  void operator()(choice_menu_head_t& head, std::vector<choice_menu_option_t>& rows,
                  const std::vector<std::string>& names, std::size_t current, bool) const {
    std::get<1>(head.fParts).setText(current < names.size() ? names[current] : std::string());
    for (std::size_t i = 0; i < rows.size(); ++i) {
      auto& label = std::get<0>(rows[i].fParts);
      label.setColour(i == current ? accent : text);
      label.setBold(i == current);
    }
  }
};
template <class Choose>
using choice_menu = widgets::ChoiceMenu<Choose, choice_menu_head_t, choice_menu_option_t, show_choice_menu>;
template <class Choose>
inline auto make_choice_menu(const palette& colours, std::string label,
                             std::vector<std::string> names, std::size_t current, Choose choose) {
  auto head = choice_menu_head(colours, std::move(label), current < names.size() ? names[current] : std::string());
  auto rows = std::ranges::to<std::vector>(std::views::transform(names, [&](const std::string& name) {
    return choice_menu_option(colours, name);
  }));
  return choice_menu<Choose>(std::move(names), current, std::move(choose), std::move(head), std::move(rows),
      show_choice_menu{colours.text, colours.accent});
}

// The items of the space bars -- Home, Direct messages, each space -- each
// with where it is: the side bar, the top one, both, or hidden.
template <class Actions>
struct spaces_choices : skiff::compose::Stacked {
  struct pick_bars {
    using Answer = std::optional<::mux::ui::request::set_space_bars>;
    std::string account;
    config::space_item_t item;
    std::optional<::mux::ui::request::set_space_bars> operator()(std::size_t index) {
      static constexpr std::array<std::pair<bool, bool>, 4> kWays{{{true, false}, {false, true}, {true, true}, {false, false}}};
      if (index < kWays.size())
        return ::mux::ui::request::set_space_bars{account, item, kWays[index].first, kWays[index].second};
      return std::nullopt;
    }
  };
  struct row : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text name;
      choice_menu<pick_bars> where;
    } parts;
    row(const palette& colours, const std::string& account, const space_item_shown& one)
        : Stacked(skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 20.0f, 4.0f, 20.0f}})),
          parts{.name = nodes::Text(one.name, 14.0f, colours.text),
                .where = make_choice_menu<pick_bars>(colours, "", {"Side bar", "Top bar", "Both bars", "Hidden"},
                                                one.side && !one.top   ? 0
                                                : one.top && !one.side ? 1
                                                : one.side && one.top  ? 2
                                                                       : 3,
                                                pick_bars{account, one.item})} {}
  };
  struct parts_t {
    std::vector<row> rows;
  } parts;
  spaces_choices(const palette& colours, const ui_shared& shared)
      : Stacked(skiff::compose::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.rows = shared.space_items |
                      std::views::transform([&](const space_item_shown& one) { return row(colours, shared.space_account, one); }) |
                      std::ranges::to<std::vector>()} {}
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
[[nodiscard]] inline skiff::compose::Look setting_row() {
  return skiff::compose::hbox(4.0f, {.fillX = true, .height = 36.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
}
[[nodiscard]] inline nodes::Text setting_label(nodes::Text label) {
  return skiff::compose::styled({.grow = scene::axes::kX, .shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                                elided(std::move(label)));
}
// A segment of a setting's row: as wide as said, in the middle of the row.
template <class Segment>
[[nodiscard]] Segment setting_choice(float width, Segment one) {
  return skiff::compose::styled({.width = width, .alignSelf = scene::align::kMiddle}, std::move(one));
}

// What a level holds of room events -- all of them or messages only, and
// each kind chosen apart -- in each part that keeps one: every chat's
// history settings, an account's own, a chat's own.
inline room_events_held events_of(const config::history_settings& every) { return {every.show_room_events, every.room_event_kinds}; }
inline room_events_held events_of(const config::account_shared& account) { return {account.room_events, account.room_event_kinds}; }
inline room_events_held events_of(const config::chat_choices& chat) { return {chat.room_events, chat.room_event_kinds}; }
inline void events_in(config::history_settings& every, const room_events_held& now) {
  every.show_room_events = now.all.value_or(true);
  every.room_event_kinds = now.kinds;
}
inline void events_in(config::account_shared& account, const room_events_held& now) {
  account.room_events = now.all;
  account.room_event_kinds = now.kinds;
}
inline void events_in(config::chat_choices& chat, const room_events_held& now) {
  chat.room_events = now.all;
  chat.room_event_kinds = now.kinds;
}
// The ways a level can be, in the dropdown's order, from As above.
inline constexpr std::size_t kEventsAbove = 0, kEventsAll = 1, kEventsMessages = 2, kEventsCustom = 3;
[[nodiscard]] inline std::size_t events_way_of(const choice_level_t& level, const room_events_held& held) {
  const bool custom = held.kinds && std::ranges::any_of(all_room_events, [&](const room_event_t& kind) {
                        return logic::choice_of(held.kinds, kind).has_value();
                      });
  if (custom)
    return kEventsCustom;
  if (!held.all)
    return has_level_above(level) ? kEventsAbove : kEventsAll;
  return *held.all ? kEventsAll : kEventsMessages;
}

// Each button edits its bound owner. The window tries several models for a
// press; an Own change defers to the right one without leaking a UI event
// to the application's request dispatcher when a scope is in another model.
template <class Owner>
inline Owner with_events_way(const Owner& now, const choice_level_t& level, std::size_t way) {
  room_events_at(level) = events_of(now);
  const auto effective = events_in_effect(level);
  room_events_held held;
  if (way == kEventsAll) held.all = true;
  else if (way == kEventsMessages) held.all = false;
  else if (way == kEventsCustom)
    held.kinds = std::ranges::fold_left(all_room_events, config::room_event_kinds{},
        [&](config::room_event_kinds kinds, const room_event_t& kind) {
          logic::choice_in(kinds, kind) = effective.shows(kind);
          return kinds;
        });
  Owner next = now;
  events_in(next, held);
  return next;
}
template <class Owner>
inline bool event_kind_shown(const Owner& now, const choice_level_t& level, const room_event_t& kind) {
  room_events_at(level) = events_of(now);
  const auto way = events_way_of(level, events_of(now));
  if (way == kEventsAll || way == kEventsMessages) return way == kEventsAll;
  return (way == kEventsAbove ? events_above(level) : events_in_effect(level)).shows(kind);
}
// Changes are data returned from the dropdown's bound node, never raw
// application requests. Trying another model safely defers the change.
template <class Owner> struct events_way_change {
  choice_level_t level;
  std::size_t way;
  void operator()(Owner& now) const { now = with_events_way(now, level, way); }
};
template <class Owner> struct choose_events_way {
  using Answer = skiff::bind::Own<events_way_change<Owner>>;
  choice_level_t level;
  Answer operator()(std::size_t index) const {
    return skiff::bind::own(events_way_change<Owner>{level, index + (has_level_above(level) ? 0 : 1)});
  }
};
template <class Owner> struct event_kind_change {
  choice_level_t level;
  room_event_t kind;
  std::size_t choice;
  void operator()(Owner& now) const {
    auto held = events_of(now);
    if (events_way_of(level, held) != kEventsCustom) return;
    if (!held.kinds) held.kinds.emplace();
    logic::choice_in(*held.kinds, kind) = choice == 0 ? std::nullopt : std::optional(choice == 1);
    events_in(now, held);
  }
};
template <class Owner> struct set_event_kind {
  using Answer = skiff::bind::Own<event_kind_change<Owner>>;
  choice_level_t level;
  room_event_t kind;
  bool show;
  Answer operator()() const { return skiff::bind::own(event_kind_change<Owner>{level, kind, show ? 1u : 2u}); }
};
template <class Owner>
inline auto event_kind_button(const palette& colours, choice_level_t level, room_event_t kind, bool show) {
  namespace c = skiff::compose;
  return c::spec_for<Owner>([level, kind, show](const Owner& now) {
    return scene::Spec{.selected = event_kind_shown(now, level, kind) == show};
  }, c::onPress(set_event_kind<Owner>{level, kind, show}, c::column(
      c::justified(c::vbox(0.0f, {.width = 70.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle,
          .hoverBackground = colours.chosen, .selectedBackground = colours.accent,
          .focusBackground = colours.chosen}), nodes::justify::middle{}),
      c::shown_for<Owner>([level, kind, show](const Owner& now) {
        return event_kind_shown(now, level, kind) == show;
      }, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(show ? "Show" : "Hide", 13.0f, colours.on_accent, true))),
      c::shown_for<Owner>([level, kind, show](const Owner& now) {
        return event_kind_shown(now, level, kind) != show;
      }, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(show ? "Show" : "Hide", 13.0f, colours.text, true)))), show ? "Show" : "Hide"));
}
template <class Owner>
inline auto event_kind_row(const palette& colours, choice_level_t level, room_event_t kind) {
  namespace c = skiff::compose;
  return c::spec_for<Owner>([level](const Owner& now) {
    const bool custom = events_way_of(level, events_of(now)) == kEventsCustom;
    return scene::Spec{.alpha = custom ? 1.0f : 0.4f, .disabled = !custom};
  }, c::row(setting_row(),
      setting_label(nodes::Text(spl::visit([](auto one) { return std::string(label_of(one)); }, kind), 14.0f, colours.text)),
      event_kind_button<Owner>(colours, level, kind, true), event_kind_button<Owner>(colours, level, kind, false)));
}
template <class Owner>
inline auto event_kinds_field(const palette& colours, choice_level_t level) {
  namespace c = skiff::compose;
  auto names = std::vector<std::string>{"All events", "Messages only", "Custom"};
  if (has_level_above(level)) names.insert(names.begin(), "As above");
  auto mode = c::projected<Owner>([level](const Owner& now) {
    room_events_at(level) = events_of(now);
    return events_way_of(level, events_of(now)) - (has_level_above(level) ? 0 : 1);
  }, make_choice_menu(colours, "Room events", std::move(names), 0, choose_events_way<Owner>{level}));
  mode.apply({.margin = {0.0f, 20.0f, 4.0f, 20.0f}});
  auto rows = std::ranges::to<std::vector>(std::views::transform(all_room_events, [&](const room_event_t& kind) {
    return event_kind_row<Owner>(colours, level, kind);
  }));
  return c::scoped<Owner>(c::handlers(), c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::move(mode), c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows))));
}

// How far a search for a message jumped to pages back, at one level, as
// the model holds it: a few numbers of events and No limit, and, where a
// level above decides for it, Default. Bound to an optional count unsaid as
// above, or to every chat's count.
inline std::optional<std::int64_t> said_of(std::int64_t now) { return now; }
inline std::optional<std::int64_t> said_of(const std::optional<std::int64_t>& now) { return now; }
inline std::int64_t count_of(std::optional<std::int64_t> chosen, std::type_identity<std::int64_t>) { return chosen.value_or(5000); }
inline std::optional<std::int64_t> count_of(std::optional<std::int64_t> chosen, std::type_identity<std::optional<std::int64_t>>) {
  return chosen;
}
template <class T>
struct jump_search_field : skiff::compose::Stacked {
  static constexpr std::array<std::int64_t, 4> kChoices{500, 5000, 50000, 0};
  // A count pressed: set as the part holds it.
  struct pick {
    using Answer = skiff::bind::Own<skiff::model::SetTo<T>>;
    std::optional<std::int64_t> most;
    Answer operator()() const { return skiff::bind::own(skiff::model::setTo(count_of(most, std::type_identity<T>{}))); }
  };
  struct parts_t {
    nodes::Text label;
    segment<pick> fallback;
    std::vector<segment<pick>> choices;
  } parts;
  bool top = false;
  [[nodiscard]] static std::string label_of(std::int64_t most) {
    return most == 0 ? std::string("No limit") : std::format("{}", most);
  }
  jump_search_field(const palette& colours, choice_level_t level)
      : Stacked(setting_row()),
        parts{.label = setting_label(nodes::Text("Look back for a message", 14.0f, colours.text)),
              .fallback = skiff::compose::visible(has_level_above(level), setting_choice(64.0f, segment<pick>(colours, "Default", {std::nullopt}))),
              .choices = kChoices | std::views::transform([&](std::int64_t most) {
                           return setting_choice(64.0f, segment<pick>(colours, label_of(most), pick{most}));
                         }) |
                         std::ranges::to<std::vector>()},
        top(!has_level_above(level)) {}
  void read(const T& now) {
    const std::optional<std::int64_t> said = said_of(now);
    const std::optional<std::int64_t> shown = top ? std::optional<std::int64_t>(said.value_or(5000)) : said;
    parts.fallback.set_active(!shown);
    for (std::size_t i = 0; i < kChoices.size(); ++i)
      parts.choices[i].set_active(shown == kChoices[i]);
  }
};

// Notifications, the same rows at every level: on, of mentions alone, the
// sender's name, the text, a sound -- each a row bound to its field.
template <class Which>
struct notify_setting_base {
  static constexpr bool unsaid = Which::unsaid;
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

// Link previews: shown, where nothing says otherwise.
struct link_previews_setting {
  static constexpr std::string_view label = "Link previews";
  static constexpr std::string_view yes = "Show", no = "Hide";
  static constexpr bool unsaid = true;
};
// Read receipts as faces: not, where nothing says otherwise.
struct receipts_setting {
  static constexpr std::string_view label = "Read receipts as faces";
  static constexpr std::string_view yes = "Show", no = "Hide";
  static constexpr bool unsaid = false;
};
// Link previews fetched from the sites themselves, through the account's
// proxy, or through its server: the server, where nothing says
// otherwise.
struct previews_direct_setting {
  static constexpr std::string_view label = "Fetch link previews";
  static constexpr std::string_view yes = "From site", no = "Server";
  static constexpr bool unsaid = false;
};
// Others told one is typing -- never what: sent, where nothing says
// otherwise.
struct typing_setting {
  static constexpr std::string_view label = "Send typing notifications";
  static constexpr std::string_view yes = "Send", no = "Don't";
  static constexpr bool unsaid = true;
};

// A setting at a level, as the model holds it: its row of Default (where
// there is a level above), Show and Hide, bound to its part -- an optional
// bool, unsaid as the level above says; a bool, said at every chat's. At
// every chat's level, an optional one shows what unsaid means there.
inline std::optional<bool> shown_of(bool now) { return now; }
inline std::optional<bool> shown_of(const std::optional<bool>& now) { return now; }
inline bool part_of(std::optional<bool> chosen, std::type_identity<bool>) { return chosen.value_or(false); }
inline std::optional<bool> part_of(std::optional<bool> chosen, std::type_identity<std::optional<bool>>) { return chosen; }
// Part: what a press sets -- the part shown, else one holding it, which
// gives each segment what it sets as it reads the model.
template <class Setting, class T, class Part = T>
struct show_hide_field : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text label;
    segment<sets<Part>> fallback, show, hide;
  } parts;
  bool top = false;
  show_hide_field(const palette& colours, choice_level_t level)
      : Stacked(setting_row()),
        parts{.label = setting_label(nodes::Text(std::string(Setting::label), 14.0f, colours.text)),
              .fallback = skiff::compose::visible(has_level_above(level), setting_choice(70.0f, segment<sets<Part>>(colours, "Default", {}))),
              .show = setting_choice(70.0f, segment<sets<Part>>(colours, std::string(Setting::yes), {})),
              .hide = setting_choice(70.0f, segment<sets<Part>>(colours, std::string(Setting::no), {}))},
        top(!has_level_above(level)) {}
  // Which is lit, and what each sets.
  void show_value(std::optional<bool> shown) {
    parts.fallback.set_active(!shown);
    parts.show.set_active(shown == true);
    parts.hide.set_active(shown == false);
  }
  void set_nexts(Part fallback, Part show, Part hide) {
    parts.fallback.act.next = std::move(fallback);
    parts.show.act.next = std::move(show);
    parts.hide.act.next = std::move(hide);
  }
  void read(const T& now) {
    const std::optional<bool> said = shown_of(now);
    this->show_value(top ? std::optional<bool>(said.value_or(Setting::unsaid)) : said);
    constexpr std::type_identity<T> as{};
    this->set_nexts(part_of(std::nullopt, as), part_of(true, as), part_of(false, as));
  }
};

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

// A notification as mux shows it itself, as Telegram Desktop's own: a card
// in a small window of its own -- the chat's avatar beside the title over
// the text.
inline auto toast_card(const palette& colours, std::string key, std::string title, std::string text) {
  return skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fill = true, .padding = {12.0f, 14.0f, 12.0f, 14.0f}, .background = colours.sidebar,
          .border = scene::Border{colours.band, 1.0f}}),
      avatar_mark(key, title, 44.0f), two_lines(colours, title, std::move(text), 14.0f, 4.0f));
}
using toast_card_t = decltype(toast_card(std::declval<const palette&>(), "", "", ""));

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
struct context_bar : skiff::compose::Stacked {
  static constexpr float kHeight = 49.0f;  // historyReplyHeight
  static constexpr float kSkip = 51.0f;    // historyReplySkip
  struct lines_column : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text title;
      nodes::Text line;
    } parts;
    explicit lines_column(const palette& colours)
        : Stacked(skiff::compose::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle})),
          parts{.title = skiff::compose::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.accent, true))),
                .line = skiff::compose::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.text)))} {}
  };
  using cancel_button = icon_button<Cancel>;
  struct parts_t {
    nodes::Icon mark;  // in the left column
    lines_column lines;
    cancel_button cancel;
  } parts;
  context_bar(const palette& colours, Cancel cancel)
      : Stacked(skiff::compose::hbox(8.0f, {.fillX = true, .height = kHeight, .padding = {0.0f, 8.0f, 0.0f, 0.0f}})),
        parts{.mark = skiff::compose::styled({.fillY = true, .width = kSkip - 8.0f}, nodes::Icon(IconShape{}, colours.accent)),
              .lines = lines_column(colours),
              .cancel = skiff::compose::styled({.alignSelf = scene::align::kMiddle}, cancel_button(colours, icon::close{}, std::move(cancel)))} {
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
struct count_badge : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text count;
  } parts;
  explicit count_badge(const palette& colours)
      : Stacked(skiff::compose::justified(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopCentre,
                                                                      .y = -10.0f,
                                                                      .height = 18.0f,
                                                                      .autoSize = scene::axes::kX,
                                                                      .minWidth = 20.0f,
                                                                      .padding = {1.0f, 5.0f, 1.0f, 5.0f},
                                                                      .cornerRadius = 9.0f,
                                                                      .background = colours.accent}),
                                          nodes::justify::middle{})),
        parts{.count = skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 11.0f, colours.on_accent, true))} {}
};

// A label, and a small button on its right that is there only where it
// does something: an element's look over a way back to the bubbles'.
template <class Act>
struct label_button_row : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text label;
    button_for<Act> reset;
  } parts;
  label_button_row(const palette& colours, std::string label, std::string button, Act act, bool shown)
      : Stacked(skiff::compose::hbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.label = skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, nodes::Text(std::move(label), 13.0f, colours.text)),
              .reset = skiff::compose::visible(shown, skiff::compose::styled({.width = 96.0f, .height = 26.0f},
                                                                             button_for<Act>(colours.widgets, std::move(button), std::move(act))))} {}
};

// A run of things under a name, as the emoji and sticker panels list them:
// the name, dim, over its cells, wrapped as wide as it is. The cells are put
// in by what it is a section of.
template <class Cell>
auto cell_section(const palette& colours, std::string name, std::vector<Cell> cells,
                  std::optional<emote_pack> pack = std::nullopt, std::optional<account_id> by = std::nullopt) {
  namespace c = skiff::compose;
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = 32.0f, .padding = {0.0f, 7.0f, 0.0f, 7.0f}}),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                    nodes::Text(std::move(name), 13.0f, colours.dim, true)),
          c::visible(pack && pack->chat,
              c::onClick(request::adopt_pack{pack.value_or(emote_pack{}), by},
                  c::styled({.width = 100.0f, .height = 26.0f, .alignSelf = scene::align::kMiddle,
                              .cornerRadius = 6.0f, .hoverBackground = colours.chosen},
                            nodes::Text("Add to account", 11.0f, colours.accent)), "Add pack to account"))),
      c::styled({.fillX = true, .autoSize = scene::axes::kY},
          nodes::Flow<std::vector<Cell>>({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, std::move(cells))));
}
template <class Cell>
using cell_section_t = decltype(cell_section(std::declval<const palette&>(), "", std::vector<Cell>{}));

inline auto& cell_section_cells(auto& section) {
  return std::get<0>(std::get<1>(section.fParts).fChildren);
}

// A panel's footer of tabs, one for each of its sections.
template <class Tab>
auto tab_strip(skiff::compose::Look look) { return skiff::compose::many(std::move(look), std::vector<Tab>{}); }
template <class Tab>
using tab_strip_t = decltype(tab_strip<Tab>(skiff::compose::vbox()));

// A dialog's buttons, at its bottom right: Cancel, and what it does --
// Send, Save, Create room -- the primary one.
template <class Cancel, class Confirm>
struct dialog_buttons : skiff::compose::Stacked {
  struct parts_t {
    widgets::Button<Cancel> cancel;
    widgets::Button<Confirm> confirm;
  } parts;
  dialog_buttons(const palette& colours, std::string confirm, Cancel cancel_it, Confirm confirm_it, float width = 96.0f)
      : Stacked(skiff::compose::justified(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {8.0f, 0.0f, 0.0f, 0.0f}}),
                                          nodes::justify::end{})),
        parts{.cancel = skiff::compose::styled({.width = width, .height = 36.0f}, widgets::Button<Cancel>(colours.widgets, "Cancel", std::move(cancel_it))),
              .confirm = skiff::compose::styled({.width = width, .height = 36.0f},
                                                primary(widgets::Button<Confirm>(colours.widgets, std::move(confirm), std::move(confirm_it))))} {}
};

// A row's top line, as a chat's in the list: the name, bold, as long as it
// can be, and the time on the right.
struct name_time_line : skiff::compose::Stacked {
  struct parts_t {
    nodes::Text name;
    nodes::Text time;
  } parts;
  name_time_line(std::string name, std::string time, skia::SkColor name_colour, skia::SkColor time_colour, float time_size)
      : Stacked(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY})),
        parts{.name = skiff::compose::styled({.grow = scene::axes::kX}, elided(nodes::Text(std::move(name), 13.0f, name_colour, true))),
              .time = nodes::Text(std::move(time), time_size, time_colour)} {}
};

// An accent's circle: its colour, a ring where it is the one in use; pressed,
// Choose is told it.
template <class Choose>
struct accent_circle : skiff::compose::Specced {
  Choose choose;
  config::accent_t accent;
  bool chosen = false;
  // Its shade in the theme in use.
  skia::SkColor shade;
  struct parts_t {
    nodes::Box<> dot;
  } parts;
  accent_circle(Choose what, config::accent_t which, const config::theme_t& in)
      : Specced({.width = 34.0f, .height = 34.0f, .cornerRadius = 17.0f}),
        choose(std::move(what)),
        accent(which),
        shade(colour_of(which, in)),
        parts{.dot = skiff::compose::styled({.place = scene::anchor::kCentre, .width = 24.0f, .height = 24.0f, .cornerRadius = 12.0f},
                                            nodes::Box<>(shade))} {}
  // A ring in its shade while it is the one in use.
  void set_chosen(bool on) {
    chosen = on;
    fState.apply({.border = scene::Border{on ? shade : 0u, on ? 2.0f : 0.0f}});
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act_on(fState, choose, accent);
    return true;
  }
  auto onPress()
    requires skiff::scene::Answering<Choose>
  {
    return choose(accent);
  }
};

// Telegram's eight accents in a row -- the theme's own first, where it is
// one to choose: the window's accent, an account's colour.
template <class Choose>
struct accent_circles : skiff::compose::Stacked {
  struct parts_t {
    std::vector<accent_circle<Choose>> circles;
  } parts;
  // Telegram's eight, in its order -- after the theme's own, where it is
  // one to choose.
  [[nodiscard]] static std::vector<config::accent_t> accents_of(bool with_theme_own) {
    std::vector<config::accent_t> all;
    if (with_theme_own)
      all.emplace_back(config::accent::theme_own{});
    const std::initializer_list<config::accent_t> eight{
        config::accent_t{config::accent::blue{}},   config::accent_t{config::accent::green{}},  config::accent_t{config::accent::pink{}},
        config::accent_t{config::accent::orange{}}, config::accent_t{config::accent::purple{}}, config::accent_t{config::accent::red{}},
        config::accent_t{config::accent::grey{}},   config::accent_t{config::accent::gold{}}};
    all.insert(all.end(), eight.begin(), eight.end());
    return all;
  }
  accent_circles(Choose choose, const config::theme_t& in, bool with_theme_own)
      : Stacked(skiff::compose::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}})),
        parts{.circles = accents_of(with_theme_own) |
                         std::views::transform([&](const config::accent_t& one) { return accent_circle<Choose>(choose, one, in); }) |
                         std::ranges::to<std::vector>()} {}
  void show_chosen(const config::accent_t& now) {
    for (auto& circle : parts.circles)
      circle.set_chosen(circle.accent == now);
  }
};

// A row longer than its room, moved sideways -- by a finger or a mouse
// dragged along it, flicked to glide on, or a sideways wheel -- and cut to
// its room. Up and down are left to the page round it: a gesture going more
// that way than along is not taken, nor is an upright wheel.
template <class Line> struct side_scroll : skiff::compose::Stacked {
  struct parts_t {
    Line line;
  } parts;
  explicit side_scroll(Line line)
      : Stacked(skiff::compose::hbox(0.0f, {.masking = true})),
        parts{.line = std::move(line)} {}
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
