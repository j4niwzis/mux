// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:controls -- The small controls: marks, rows, icon buttons, headers, segments, the drawer button.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
export module mux.ui.base:controls;

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
import skiff.widgets.textbox;
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
MUX_INLINE_IN_RELEASE auto icon_mark(const palette& colours, icon_t mark = icon::none{}, float width = 28.0f, float height = 36.0f) {
  return SKIFF_NAMED(icon_mark, skiff::compose::styled({.width = width, .height = height, .alignSelf = scene::align::kMiddle},
                                nodes::Icon(shape_of(mark), colours.dim)));
}
using icon_mark_t = decltype(icon_mark(std::declval<const palette&>()));
// A radio's ring, with a dot in it while it is the one chosen: in the accent
// then, dim otherwise.
[[nodiscard]] inline IconShape radio_shape(bool chosen) {
  IconShape out{{{nodes::mark::circle{0.0f, 0.0f, 8.0f}, 2.0f}}};
  if (chosen)
    out.marks.push_back({nodes::mark::circle{0.0f, 0.0f, 4.0f}, 0.0f, true});
  return out;
}
MUX_INLINE_IN_RELEASE auto radio_dot(const palette& colours, bool chosen) {
  return SKIFF_NAMED(radio_dot, skiff::compose::styled({.width = 20.0f, .height = 20.0f, .alignSelf = scene::align::kMiddle},
                                nodes::Icon(radio_shape(chosen), chosen ? colours.accent : colours.dim)));
}
// The same, chosen as the model says: `chosen` makes it of the part Want.
template <class Want, class Chosen>
auto radio_dot_for(const palette& colours, Chosen chosen) {
  return SKIFF_NAMED(radio_dot_for, skiff::compose::applied<Want>(
      std::move(chosen),
      [at = &colours](nodes::Icon& dot, bool on) {
        dot.setShape(radio_shape(on));
        dot.setColour(on ? at->accent : at->dim);
      },
      radio_dot(colours, false)));
}

// A round avatar of a size, in a row: a chat's or a person's (Kind, which
// rounding it takes), by their id and the name its initials come from.
template <class Kind>
auto avatar_mark(Kind kind, std::string id, std::string_view shown, float size) {
  auto avatar = skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                                       widgets::Avatar<from_avatars>(initials_of(shown), size, picture_of(id), gradient_of(id)));
  round_avatar(avatar, size, kind);
  return avatar;
}
template <class Kind>
using avatar_mark_t = decltype(avatar_mark(Kind{}, std::string(), std::string_view(), 0.0f));
// Where a face goes -- a person's or a chat's (Kind), anywhere in the core or
// a plugin: a piece, keyed by its kind, its facts whose it is and how big.
// Alone it makes nothing, taking no room; the avatars plugin's rule makes
// the face in its place (instead). So with that plugin off, no view draws
// a face, and no plugin need know another.
struct face_said {
  std::string id;
  std::string name;
  float size = 0.0f;
};
template <class Kind>
struct face_fn : skiff::compose::piece<face_fn<Kind>> {
  using facts_type = face_said;
  static face_said facts(const std::string& id, std::string_view name, float size) { return {id, std::string(name), size}; }
  auto make(const std::string&, std::string_view, float) const {
    return skiff::compose::visible(false, nodes::Box<>(skia::SkColor{0}));
  }
};
template <class Kind>
auto face_spot(Kind, std::string id, std::string_view name, float size) {
  return face_fn<Kind>{}(std::move(id), name, size);
}
// Whose an avatar is, as the model says: the id it is found by, and the name
// its initials come from.
struct avatar_who {
  std::string id;
  std::string name;
  friend bool operator==(const avatar_who&, const avatar_who&) = default;
};
// An avatar showing whoever `who` makes of the part Want, as it moves.
template <class Want, class Kind, class Who>
auto avatar_for(Kind kind, float size, Who who) {
  return SKIFF_NAMED(avatar_for, skiff::compose::applied<Want>(
      std::move(who),
      [](widgets::Avatar<from_avatars>& avatar, const avatar_who& now) {
        avatar.show(initials_of(now.name), picture_of(now.id), gradient_of(now.id));
      },
      avatar_mark(kind, std::string(), std::string_view(), size)));
}
// An avatar that opens: pressed, its picture in the viewer, where it can be
// looked at whole and saved -- a person's in their card, a chat's in its
// info. One with no id asks for nothing.
struct open_avatar_of {
  using Answer = std::optional<request::open_avatar>;
  std::string id;
  Answer operator()() const { return id.empty() ? Answer() : Answer(request::open_avatar{id}); }
};
// What a rule on a piece about someone sees: whose it is -- a person's or a
// room's id, and their name (the avatars plugin's picture of them).
struct face_facts {
  std::string id;
  std::string name;
};
template <class Kind>
auto avatar_button(Kind kind, std::string id, std::string_view shown, float size) {
  return SKIFF_NAMED(avatar_button, skiff::compose::onPress(open_avatar_of{id}, avatar_mark(kind, id, shown, size), "Open picture"));
}

// A name over how it is: two lines, each cut where it runs out of room,
// taking what their row leaves them.
struct two_line_style {
  float second_size = 0.0f;
  std::optional<skia::SkColor> first_ink;
  std::optional<skia::SkColor> second_ink;
  bool selectable = false;
  std::optional<bool> show_second;
};
MUX_INLINE_IN_RELEASE auto two_lines(const palette& colours, std::string first, std::string second,
                      float size, float gap, two_line_style style = {}) {
  return SKIFF_NAMED(two_lines, skiff::compose::column(
      skiff::compose::vbox(gap, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
      skiff::compose::styled({.fillX = true}, elided(nodes::Text(std::move(first), size,
          style.first_ink.value_or(colours.text), true, style.selectable))),
      skiff::compose::visible(style.show_second.value_or(!second.empty()),
          skiff::compose::styled({.fillX = true}, elided(nodes::Text(second,
              style.second_size > 0.0f ? style.second_size : size - 2.0f,
              style.second_ink.value_or(colours.dim), false, style.selectable))))));
}
MUX_INLINE_IN_RELEASE auto two_lines(const palette& colours, std::string first, std::string second,
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

// A line of a list or a menu as tall as its look says, as wide as what holds
// it: its icon on the left where it has one, its text taking the room, and
// a radio at the end where it is one of a choice (`choice`: whether it is the
// one chosen). It lights under the pointer and the keyboard's focus; a press
// asks what `act` answers.
inline constexpr float kRowHeight = 46.0f;
template <class Act>
  requires skiff::scene::Answering<Act>
auto row_item(const palette& colours, std::string text, Act act, icon_t icon = icon::none{},
              std::optional<bool> choice = std::nullopt, row_look look = {}) {
  namespace c = skiff::compose;
  std::string label = text;
  return SKIFF_NAMED(row_item, c::said(std::nullopt, choice.value_or(false),
      c::onPress(std::move(act),
          c::row(c::hbox(look.gap, {.fillX = true, .height = look.height, .padding = look.padding,
                                    .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
                                    .focusBackground = colours.chosen}),
                 c::visible(spl::visit([](auto one) { return drawn(one); }, icon), icon_mark(colours, icon, look.icon_width, look.icon_height)),
                 c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                           elided(nodes::Text(std::move(text), look.text_size, colours.text))),
                 c::visible(choice.has_value(), radio_dot(colours, choice.value_or(false)))),
          std::move(label))));
}
template <class Act>
using row_item_t = decltype(row_item<Act>(std::declval<const palette&>(), std::string(), std::declval<Act>()));

// A round button with only an icon in it -- back, close, send -- lit under
// the pointer or the keyboard's focus; a press asks what `act` answers. Its
// icon in the text's colour unless said, and named by it unless said.
template <class Act>
  requires skiff::scene::Answering<Act>
auto icon_button(const palette& colours, icon_t mark, Act act, std::optional<skia::SkColor> ink = std::nullopt,
                 std::string label = {}) {
  namespace c = skiff::compose;
  if (label.empty())
    label = std::string(name_of(mark));
  return SKIFF_NAMED(icon_button, c::onPress(std::move(act),
      c::column(c::vbox(0.0f, {.width = 36.0f, .height = 36.0f, .cornerRadius = 18.0f, .hoverBackground = colours.chosen,
                               .focusBackground = colours.chosen}),
                c::styled({.fill = true}, nodes::Icon(shape_of(mark), ink.value_or(colours.text)))),
      std::move(label)));
}
template <class Act>
using icon_button_t = decltype(icon_button<Act>(std::declval<const palette&>(), icon_t{}, std::declval<Act>()));

// A page with nowhere to go back to: its header's ← hidden, and pressing
// it does nothing.
struct no_action {
  using Answer = skiff::scene::Taken;
  skiff::scene::Taken operator()() const { return {}; }
};
// A link of Settings' home page: its icon and name; pressed, what it opens
// (mux.ui:settings_pages, and the plugins' links: contract::settings_links).
template <class Event>
auto settings_link(const palette& colours, std::string label, icon_t icon, Event event) {
  return SKIFF_NAMED(settings_link, skiff::compose::onClick(std::move(event), skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f},
                                  .hoverBackground = colours.chosen, .focusBackground = colours.chosen}),
      skiff::compose::styled({.width = 28.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon), colours.dim)),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(label, 15.0f, colours.text)))), label));
}
using no_back = no_action;

// The head of a page: ← on the left where there is somewhere to go back to,
// the page's name, and ✕ on the right where the page closes. Every panel,
// box and page that has a title and a ✕ has this one.
inline constexpr float kPageHeaderHeight = 54.0f;
// A chat's head: as high as Telegram's, and what stands in its place.
inline constexpr float kChatHeadHeight = 56.0f;
template <class Back, class Close>
auto page_header(const palette& colours, std::string name, Back back, Close close, bool has_back, bool has_close) {
  return SKIFF_NAMED(page_header, skiff::compose::row(
      skiff::compose::hbox(12.0f, {.fillX = true, .height = kPageHeaderHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}}),
      skiff::compose::visible(has_back, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button<Back>(colours, icon::back{}, std::move(back)))),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                             .margin = {0.0f, 0.0f, 0.0f, has_back ? 0.0f : 10.0f}},
          elided(nodes::Text(std::move(name), 17.0f, colours.text, true))),
      skiff::compose::visible(has_close, skiff::compose::styled({.alignSelf = scene::align::kMiddle},
          icon_button<Close>(colours, icon::close{}, std::move(close))))));
}
template <class Back, class Close>
using page_header_t = decltype(page_header(std::declval<const palette&>(), std::string{}, Back{}, Close{}, false, false));

// One segment of a segmented control: its text centred, filled with the
// accent while it is the one chosen (`active`), and said to be a tab.
template <class Act>
  requires skiff::scene::Answering<Act>
auto segment(const palette& colours, std::string text, Act act, bool active) {
  namespace c = skiff::compose;
  std::string label = text;
  return SKIFF_NAMED(segment, c::said(scene::SemanticRole{scene::semantic_role::tab{}}, active,
      c::onPress(std::move(act),
          c::column(c::justified(c::vbox(0.0f, {.width = 92.0f, .height = 28.0f, .hoverBackground = colours.chosen,
                                                .selectedBackground = colours.accent, .focusBackground = colours.chosen,
                                                .selected = active}),
                                 nodes::justify::middle{}),
                    c::styled({.alignSelf = scene::align::kMiddle},
                              nodes::Text(std::move(text), 13.0f, active ? colours.on_accent : colours.text, true))),
          std::move(label))));
}

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
MUX_INLINE_IN_RELEASE auto menu_button(const palette& colours) {
  namespace c = skiff::compose;
  // Queue the request through onPress, as the other buttons do. Returning
  // it from onClick relied on the erased walks keeping handler answers;
  // Android's static release walks could consume the tap without opening.
  return SKIFF_NAMED(menu_button, c::onClick(request::open_drawer{},
      c::styled({.width = 36.0f, .height = 36.0f, .cornerRadius = 8.0f,
              .hoverBackground = colours.chosen, .focusBackground = colours.chosen},
          nodes::Icon(IconShape{{{nodes::mark::rect{-8.0f, -7.0f, 8.0f, -5.0f, 1.0f}, 0.0f, true},
                                {nodes::mark::rect{-8.0f, -1.0f, 8.0f, 1.0f, 1.0f}, 0.0f, true},
                                {nodes::mark::rect{-8.0f, 5.0f, 8.0f, 7.0f, 1.0f}, 0.0f, true}}},
                      colours.text)), "Menu"));
}

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
[[nodiscard]] constexpr std::string_view label_of(room_event::thread_answers) { return "Thread answers, in the timeline too"; }

// The existing dropdown's head and option rows, built with combinators.
// Selection and pointer capture are shared with the widget library.
MUX_INLINE_IN_RELEASE auto choice_menu_head(const palette& colours, std::string label, std::string value) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(choice_menu_head, c::row(c::hbox(8.0f, {.fillX = true, .height = 36.0f,
      .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f,
      .background = colours.tile, .hoverBackground = colours.chosen, .border = scene::Border{colours.band, 1.0f}}),
      c::visible(!label.empty(), c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(label, 13.0f, colours.dim)))),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(std::move(value), 14.0f, colours.text))),
      c::styled({.width = 16.0f, .height = 16.0f, .alignSelf = scene::align::kMiddle}, nodes::Icon(shape_of(icon::down{}), colours.dim))));
}
// An option, the one chosen in the accent and bold.
MUX_INLINE_IN_RELEASE auto choice_menu_option(const palette& colours, std::string name, bool chosen = false) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(choice_menu_option, c::row(c::hbox(0.0f, {.fillX = true, .height = 32.0f,
      .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .cornerRadius = 6.0f, .hoverBackground = colours.chosen}),
      // On one line, cut short with an ellipsis where the list is narrower
      // than the name: wrapped, a long name in a narrow list overflowed its
      // row onto the next ones (a tester's).
      c::styled({.shrink = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                elided(nodes::Text(std::move(name), 14.0f, chosen ? colours.accent : colours.text, chosen)))));
}
using choice_menu_head_t = decltype(choice_menu_head(std::declval<const palette&>(), std::string(), std::string()));
using choice_menu_option_t = decltype(choice_menu_option(std::declval<const palette&>(), std::string()));
// What a choice menu shows as its choice moves or it opens: its head made
// again with the name chosen (MakeHead), its options with the chosen one
// marked, shown while it is open -- made from the names, nothing in them
// reached into.
template <class MakeHead> struct show_choice_menu {
  const palette* colours;
  MakeHead head_of;
  template <class Head>
  void operator()(Head& head, std::vector<choice_menu_option_t>& rows, const std::vector<std::string>& names, std::size_t current,
                  bool open) const {
    head = head_of(current < names.size() ? names[current] : std::string());
    rows = names | std::views::enumerate | std::views::transform([&](const auto& each) {
             const auto& [at, name] = each;
             auto row = choice_menu_option(*colours, name, static_cast<std::size_t>(at) == current);
             row.setVisible(open);
             return row;
           }) |
           std::ranges::to<std::vector>();
  }
};
// The head with its label beside the name chosen.
struct labelled_choice_head {
  const palette* colours;
  std::string label;
  auto operator()(std::string value) const { return choice_menu_head(*colours, label, std::move(value)); }
};
template <class Choose>
using choice_menu = widgets::ChoiceMenu<Choose, choice_menu_head_t, choice_menu_option_t, show_choice_menu<labelled_choice_head>>;
template <class Choose>
MUX_INLINE_IN_RELEASE auto make_choice_menu(const palette& colours, std::string label,
                             std::vector<std::string> names, std::size_t current, Choose choose) {
  auto head = choice_menu_head(colours, label, current < names.size() ? names[current] : std::string());
  auto rows = names | std::views::transform([&](const std::string& name) {
    return choice_menu_option(colours, name);
  }) | std::ranges::to<std::vector>();
  return choice_menu<Choose>(std::move(names), current, std::move(choose), std::move(head), std::move(rows),
      show_choice_menu<labelled_choice_head>{&colours, {&colours, std::move(label)}});
}


// What a level holds of room events: all of them or messages only, and
// each kind chosen apart.
struct room_events_held {
  std::optional<bool> all;
  std::optional<config::room_event_kinds> kinds;
  friend bool operator==(const room_events_held&, const room_events_held&) = default;
};
// What each level holds, as the program last said (its looks part): a part
// of what is shown, for a list to show what is in effect where a level is
// not its own.
struct room_events_picked {
  room_events_held everywhere, account, chat;
  template <class Self>
  [[nodiscard]] auto& at(this Self& self, const choice_level_t& level) {
    return spl::visit(spl::overloaded{[&](choice_level::everywhere) -> auto& { return self.everywhere; },
                                      [&](choice_level::account) -> auto& { return self.account; },
                                      [&](choice_level::chat) -> auto& { return self.chat; }},
                      level);
  }
  friend bool operator==(const room_events_picked&, const room_events_picked&) = default;
};
// What is shown with a level as it is, and the levels over it.
[[nodiscard]] inline room_event_filter events_in_effect(const room_events_picked& now, const choice_level_t& level) {
  const room_events_held& every = now.everywhere;
  const room_events_held& account = now.account;
  const room_events_held& chat = now.chat;
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
[[nodiscard]] inline room_event_filter events_above(const room_events_picked& now, const choice_level_t& level) {
  return spl::visit(spl::overloaded{[&](choice_level::chat) { return events_in_effect(now, choice_level::account{}); },
                                    [&](const auto&) { return events_in_effect(now, choice_level::everywhere{}); }},
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

// What a level is asked to hold, set to a way: Custom starts from what
// was in effect there (the program's, which holds every level).
[[nodiscard]] inline room_events_held events_for_way(const room_events_picked& now, const choice_level_t& level, std::size_t way) {
  room_events_held held;
  if (way == kEventsAll)
    held.all = true;
  else if (way == kEventsMessages)
    held.all = false;
  else if (way == kEventsCustom)
    held.kinds = std::ranges::fold_left(all_room_events, config::room_event_kinds{}, [&](config::room_event_kinds kinds, const room_event_t& kind) {
      logic::choice_in(kinds, kind) = events_in_effect(now, level).shows(kind);
      return kinds;
    });
  return held;
}
// Whether a kind shows at a level, as its way says.
[[nodiscard]] inline bool event_kind_shown(const room_events_picked& now, const choice_level_t& level, const room_event_t& kind) {
  const auto way = events_way_of(level, now.at(level));
  if (way == kEventsAll || way == kEventsMessages)
    return way == kEventsAll;
  return (way == kEventsAbove ? events_above(now, level) : events_in_effect(now, level)).shows(kind);
}
// The way picked in the dropdown: the program sets the level so.
struct choose_events_way {
  using Answer = request::set_events_way;
  choice_level_t level;
  Answer operator()(std::size_t index) const { return {level, index + (has_level_above(level) ? 0 : 1)}; }
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
// Answered as a change of the Owner the field is scoped to -- not the part
// bound nearest the button (what it reads to light itself, room_events_picked).
template <class Owner> struct set_event_kind {
  using Answer = decltype(skiff::model::over<Owner>(std::declval<event_kind_change<Owner>>()));
  choice_level_t level;
  room_event_t kind;
  bool show;
  Answer operator()() const { return skiff::model::over<Owner>(event_kind_change<Owner>{level, kind, show ? 1u : 2u}); }
};
template <class Owner>
MUX_INLINE_IN_RELEASE auto event_kind_button(const palette& colours, choice_level_t level, room_event_t kind, bool show) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(event_kind_button, c::spec_for<room_events_picked>([level, kind, show](const room_events_picked& now) {
    return scene::Spec{.selected = event_kind_shown(now, level, kind) == show};
  }, c::onPress(set_event_kind<Owner>{level, kind, show}, c::column(
      c::justified(c::vbox(0.0f, {.width = 70.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle,
          .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
          .focusBackground = colours.chosen}), nodes::justify::middle{}),
      c::styled({.alignSelf = scene::align::kMiddle},
          nodes::Text(show ? "Show" : "Hide", 13.0f, colours.text, true))), show ? "Show" : "Hide")));
}
template <class Owner>
MUX_INLINE_IN_RELEASE auto event_kind_row(const palette& colours, choice_level_t level, room_event_t kind) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(event_kind_row, c::spec_for<room_events_picked>([level](const room_events_picked& now) {
    const bool custom = events_way_of(level, now.at(level)) == kEventsCustom;
    return scene::Spec{.alpha = custom ? 1.0f : 0.4f, .disabled = !custom};
  }, c::row(setting_row(),
      setting_label(nodes::Text(spl::visit([](auto one) { return std::string(label_of(one)); }, kind), 14.0f, colours.text)),
      event_kind_button<Owner>(colours, level, kind, true), event_kind_button<Owner>(colours, level, kind, false))));
}
template <class Owner>
MUX_INLINE_IN_RELEASE auto event_kinds_field(const palette& colours, choice_level_t level,
                              const std::vector<room_event_t>& kinds = std::vector<room_event_t>(all_room_events.begin(), all_room_events.end())) {
  namespace c = skiff::compose;
  auto names = std::vector<std::string>{"All events", "Messages only", "Custom"};
  if (has_level_above(level)) names.insert(names.begin(), "As above");
  auto mode = c::projected<room_events_picked>([level](const room_events_picked& now) {
    return events_way_of(level, now.at(level)) - (has_level_above(level) ? 0 : 1);
  }, make_choice_menu(colours, "Room events", std::move(names), 0, choose_events_way{level}));
  mode.apply({.margin = {0.0f, 20.0f, 4.0f, 20.0f}});
  auto rows = kinds | std::views::transform([&](const room_event_t& kind) {
    return event_kind_row<Owner>(colours, level, kind);
  }) | std::ranges::to<std::vector>();
  return c::visible(!kinds.empty(), c::scoped<Owner>(c::handlers(), c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::move(mode), c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)))));
}

struct pick_style_theme {
  using Answer = skiff::bind::Own<skiff::model::SetTo<std::optional<config::theme_t>>>;
  Answer operator()(std::size_t index) const {
    const std::array<std::optional<config::theme_t>, 6> values{std::nullopt, config::theme::classic{},
        config::theme::day{}, config::theme::tinted{}, config::theme::night{}, config::theme::graphite{}};
    return skiff::bind::own(skiff::model::setTo(values.at(index)));
  }
};
template <auto Member>
MUX_INLINE_IN_RELEASE auto style_text_field(const palette& colours, std::string label, std::string hint) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(style_text_field, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      nodes::Text(std::move(label), 13.0f, colours.dim),
      c::bound<skiff::model::Field<Member>>(c::styled({.fillX = true, .height = 36.0f},
          widgets::TextField<std::string>(colours.widgets, std::move(hint))))));
}
MUX_INLINE_IN_RELEASE auto style_controls(const palette& colours, bool choose_base = true) {
  namespace c = skiff::compose;
  using style = config::style_settings;
  auto theme = c::projected<skiff::model::Field<&style::theme>>([](const std::optional<config::theme_t>& now) {
    return now ? now->index() + 1 : 0;
  }, make_choice_menu(colours, "Theme", {"As above", "Classic", "Day", "Tinted", "Night", "Graphite"}, 0, pick_style_theme{}));
  return SKIFF_NAMED(style_controls, c::scoped<style>(c::handlers(), c::column(
      c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}}),
      nodes::Text("FONTS AND THEME", 13.0f, colours.dim, true), c::visible(choose_base, std::move(theme)),
      style_text_field<&style::theme_file>(colours, "Custom theme", "JSON palette file path; empty inherits"),
      style_text_field<&style::font>(colours, "Text font", "Installed family or font file path; empty inherits"),
      style_text_field<&style::monospace>(colours, "Code font", "Installed family or font file path; empty inherits"),
      c::onClick(request::apply_style{}, c::styled({.fillX = true, .height = 36.0f},
          widgets::Button<>(colours.widgets, "Apply fonts and theme"))))));
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
template <class Want, class Active, class Act>
auto setting_button(const palette& colours, std::string name, float width, Active active, Act act) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(setting_button, c::spec_for<Want>([active](const auto& now) { return scene::Spec{.selected = active(now)}; },
      c::onPress(std::move(act), c::column(c::justified(
          c::vbox(0.0f, {.width = width, .height = 28.0f, .alignSelf = scene::align::kMiddle,
              .hoverBackground = colours.chosen, .selectedBackground = colours.chosen,
              .focusBackground = colours.chosen}), nodes::justify::middle{}),
          c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(name, 13.0f, colours.text, true))), name)));
}
// Bind every segment to the named field, rather than its scalar type:
// another model walking the same window can contain many such scalars.
template <class T, class Want>
auto jump_search_field(const palette& colours, choice_level_t level) {
  namespace c = skiff::compose;
  constexpr std::array<std::int64_t, 4> counts{500, 5000, 50000, 0};
  const auto shown = [top = !has_level_above(level)](const T& now) {
    const auto said = said_of(now);
    return top ? std::optional<std::int64_t>(said.value_or(5000)) : said;
  };
  auto choices = counts | std::views::transform([&](auto most) {
    return setting_button<Want>(colours, most == 0 ? std::string("No limit") : std::format("{}", most), 64.0f,
        [shown, most](const T& now) { return shown(now) == most; },
        sets<T>{count_of(most, std::type_identity<T>{})});
  }) | std::ranges::to<std::vector>();
  return SKIFF_NAMED(jump_search_field, c::row(setting_row(),
      setting_label(nodes::Text("Look back for a message", 14.0f, colours.text)),
      c::visible(has_level_above(level), setting_button<Want>(colours, "Default", 64.0f,
          [shown](const T& now) { return !shown(now); }, sets<T>{count_of(std::nullopt, std::type_identity<T>{})})),
      c::many(c::hbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(choices))));
}

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
template <class Setting, class T, class Want>
auto show_hide_field(const palette& colours, choice_level_t level) {
  namespace c = skiff::compose;
  const auto shown = [top = !has_level_above(level)](const T& now) {
    const auto said = shown_of(now);
    return top ? std::optional<bool>(said.value_or(Setting::unsaid)) : said;
  };
  return SKIFF_NAMED(show_hide_field, c::row(setting_row(),
      setting_label(nodes::Text(std::string(Setting::label), 14.0f, colours.text)),
      c::visible(has_level_above(level), setting_button<Want>(colours, "Default", 70.0f,
          [shown](const T& now) { return !shown(now); }, sets<T>{part_of(std::nullopt, std::type_identity<T>{})})),
      setting_button<Want>(colours, std::string(Setting::yes), 70.0f,
          [shown](const T& now) { return shown(now) == true; }, sets<T>{part_of(true, std::type_identity<T>{})}),
      setting_button<Want>(colours, std::string(Setting::no), 70.0f,
          [shown](const T& now) { return shown(now) == false; }, sets<T>{part_of(false, std::type_identity<T>{})})));
}

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
// for every field that answers: the chat's composer, a thread's. Each part
// reads what `said` makes of the part Want (an optional compose_context) --
// none, and the bar is not there; Cancel is what its ✕ asks.
inline constexpr float kContextHeight = 49.0f;  // historyReplyHeight
inline constexpr float kContextSkip = 51.0f;    // historyReplySkip
template <class Want, class Said, class Cancel>
auto context_bar(const palette& colours, Said said, Cancel cancel) {
  namespace c = skiff::compose;
  const auto line = [said](std::string compose_context::* which) {
    return [said, which](const auto& now) {
      const std::optional<compose_context> shown = said(now);
      return shown ? (*shown).*which : std::string();
    };
  };
  return SKIFF_NAMED(context_bar, c::shown_for<Want>([said](const auto& now) { return said(now).has_value(); },
      c::row(c::hbox(8.0f, {.fillX = true, .height = kContextHeight, .padding = {0.0f, 8.0f, 0.0f, 0.0f}}),
             c::applied<Want>(said,
                              [](nodes::Icon& mark, const std::optional<compose_context>& now) {
                                mark.setShape(now ? shape_of(now->mark) : IconShape{});
                              },
                              c::styled({.fillY = true, .width = kContextSkip - 8.0f}, nodes::Icon(IconShape{}, colours.accent))),
             c::column(c::vbox(2.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                       c::text_for<Want>(line(&compose_context::title),
                                         c::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.accent, true)))),
                       c::text_for<Want>(line(&compose_context::line),
                                         c::styled({.fillX = true}, elided(nodes::Text("", 13.0f, colours.text))))),
             c::styled({.alignSelf = scene::align::kMiddle}, icon_button<Cancel>(colours, icon::close{}, std::move(cancel), std::nullopt, "Cancel")))));
}

// A count on an accent badge over the top of a round button: the @ and the
// heart's, and the down arrow's. Shown while `count` makes more than none of
// the part Want.
MUX_INLINE_IN_RELEASE auto count_badge(const palette& colours, std::string count = {}) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(count_badge, c::column(c::justified(c::vbox(0.0f, {.place = scene::anchor::kTopCentre, .y = -10.0f, .height = 18.0f,
                                               .autoSize = scene::axes::kX, .minWidth = 20.0f, .padding = {1.0f, 5.0f, 1.0f, 5.0f},
                                               .cornerRadius = 9.0f, .background = colours.accent}),
                                nodes::justify::middle{}),
                   c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(std::move(count), 11.0f, colours.on_accent, true))));
}
template <class Want, class Count>
auto count_badge_for(const palette& colours, Count count) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(count_badge_for, c::shown_for<Want>([count](const auto& now) { return count(now) > 0; },
      c::column(c::justified(c::vbox(0.0f, {.place = scene::anchor::kTopCentre, .y = -10.0f, .height = 18.0f,
                                            .autoSize = scene::axes::kX, .minWidth = 20.0f, .padding = {1.0f, 5.0f, 1.0f, 5.0f},
                                            .cornerRadius = 9.0f, .background = colours.accent}),
                             nodes::justify::middle{}),
                c::text_for<Want>([count](const auto& now) { return std::to_string(count(now)); },
                                  c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 11.0f, colours.on_accent, true))))));
}

// A label, and a small button on its right that is there only where it
// does something: an element's look over a way back to the bubbles'.
template <class Act>
auto label_button_row(const palette& colours, std::string label, std::string button, Act act, bool shown) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(label_button_row, c::row(c::hbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, nodes::Text(std::move(label), 13.0f, colours.text)),
                c::visible(shown, c::styled({.width = 96.0f, .height = 26.0f}, button_for<Act>(colours.widgets, std::move(button), std::move(act))))));
}

// The same row, as what it reads of the model says: its words, and
// whether its button is there.
template <class Want, class Label, class Act, class Shown>
auto label_button_row_for(const palette& colours, Label label, std::string button, Act act, Shown shown) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(label_button_row_for, c::row(c::hbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                c::text_for<Want>(std::move(label),
                                  c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, nodes::Text("", 13.0f, colours.text))),
                c::shown_for<Want>(std::move(shown),
                                   c::styled({.width = 96.0f, .height = 26.0f}, button_for<Act>(colours.widgets, std::move(button), std::move(act))))));
}

// A run of things under a name, as the emoji and sticker panels list them:
// the name, dim, over its cells, wrapped as wide as it is. The cells are put
// in by what it is a section of.
template <class Cell>
auto cell_section(const palette& colours, std::string name, std::vector<Cell> cells,
                  std::optional<emote_pack> pack = std::nullopt, std::optional<account_id> by = std::nullopt) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(cell_section, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::row(c::hbox(8.0f, {.fillX = true, .height = 32.0f, .padding = {0.0f, 7.0f, 0.0f, 7.0f}}),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                    nodes::Text(std::move(name), 13.0f, colours.dim, true)),
          c::visible(pack && pack->chat,
              c::onClick(request::adopt_pack{pack.value_or(emote_pack{}), by},
                  c::styled({.width = 100.0f, .height = 26.0f, .alignSelf = scene::align::kMiddle,
                              .cornerRadius = 6.0f, .hoverBackground = colours.chosen},
                            nodes::Text("Add to account", 11.0f, colours.accent)), "Add pack to account"))),
      c::styled({.fillX = true, .autoSize = scene::axes::kY},
          nodes::Flow<std::vector<Cell>>({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true}, std::move(cells)))));
}
template <class Cell>
using cell_section_t = decltype(cell_section(std::declval<const palette&>(), "", std::vector<Cell>{}));



// A dialog's buttons, at its bottom right: Cancel, and what it does --
// Send, Save, Create room -- the primary one.
template <class Cancel, class Confirm>
auto dialog_buttons(const palette& colours, std::string confirm, Cancel cancel_it, Confirm confirm_it, float width = 96.0f) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(dialog_buttons, c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {8.0f, 0.0f, 0.0f, 0.0f}}),
                             nodes::justify::end{}),
                c::styled({.width = width, .height = 36.0f}, widgets::Button<Cancel>(colours.widgets, "Cancel", std::move(cancel_it))),
                c::styled({.width = width, .height = 36.0f},
                          primary(widgets::Button<Confirm>(colours.widgets, std::move(confirm), std::move(confirm_it))))));
}

// A row's top line, as a chat's in the list: the name, bold, as long as it
// can be, and the time on the right.
MUX_INLINE_IN_RELEASE auto name_time_line(std::string name, std::string time, skia::SkColor name_colour, skia::SkColor time_colour,
                           float time_size) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(name_time_line, c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                c::styled({.grow = scene::axes::kX}, elided(nodes::Text(std::move(name), 13.0f, name_colour, true))),
                nodes::Text(std::move(time), time_size, time_colour)));
}

// Telegram's eight accents, in its order -- after the theme's own, where it
// is one to choose.
[[nodiscard]] inline std::vector<config::accent_t> accents_of(bool with_theme_own) {
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
// A press of an accent's circle: what its chooser answers for that accent.
template <class Choose>
struct accent_chosen {
  Choose choose;
  config::accent_t accent;
  auto operator()() { return choose(accent); }
};
// An accent's circle: its colour in the theme in use, a ring in it where it
// is the one in use; pressed, what `choose` answers for it.
[[nodiscard]] inline scene::Spec accent_ring(skia::SkColor shade, bool on) {
  return {.border = scene::Border{on ? shade : 0u, on ? 2.0f : 0.0f}};
}
template <class Choose>
auto accent_circle(Choose choose, config::accent_t which, const config::theme_t& in, bool chosen) {
  namespace c = skiff::compose;
  const skia::SkColor shade = colour_of(which, in);
  return SKIFF_NAMED(accent_circle, c::styled(accent_ring(shade, chosen),
      c::onPress(accent_chosen<Choose>{std::move(choose), which},
          c::column(c::vbox(0.0f, {.width = 34.0f, .height = 34.0f, .cornerRadius = 17.0f}),
                    c::styled({.place = scene::anchor::kCentre, .width = 24.0f, .height = 24.0f, .cornerRadius = 12.0f},
                              nodes::Box<>(shade))),
          "Accent colour")));
}
// Telegram's eight accents in a row -- the theme's own first, where it is
// one to choose: the window's accent, an account's colour. The one in use
// ringed, as `chosen` makes it of the part Want.
template <class Want, class Choose, class Chosen>
auto accent_circles_for(Choose choose, const config::theme_t& in, bool with_theme_own, Chosen chosen) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(accent_circles_for, c::many(c::hbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}}),
                 accents_of(with_theme_own) | std::views::transform([&](const config::accent_t& one) {
                   return c::spec_for<Want>(
                       [chosen, one, shade = colour_of(one, in)](const auto& now) { return accent_ring(shade, chosen(now) == one); },
                       accent_circle(choose, one, in, false));
                 }) | std::ranges::to<std::vector>()));
}


// Someone found: their name over their ID -- a piece, about them (the
// avatars plugin's picture of them beside it).
inline constexpr struct person_pick_text_fn : skiff::compose::piece<person_pick_text_fn> {
  using facts_type = face_facts;
  static face_facts facts(const palette&, const found_person& one) { return {one.id, one.name.empty() ? one.id : one.name}; }
  auto make(const palette& colours, const found_person& one) const {
    return two_lines(colours, one.name.empty() ? one.id : one.name, one.id, 14.0f, 2.0f);
  }
} person_pick_text{};
// Someone found, as a row: pressed, the chat with them -- in Start chat, and
// in the chat list where nothing joined matches. A builder (its text a
// piece): built where the program's rules are known.
template <class Event>
MUX_INLINE_IN_RELEASE auto person_pick_row(const palette& colours, const found_person& one, Event event) {
  namespace c = skiff::compose;
  const std::string name = one.name.empty() ? one.id : one.name;
  return SKIFF_NAMED(person_pick_row, c::onClick(std::move(event), c::row(
      c::hbox(12.0f, {.fillX = true, .height = 52.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f},
          .cornerRadius = 8.0f, .hoverBackground = colours.chosen}),
      person_pick_text(colours, one)), name));
}
MUX_INLINE_IN_RELEASE auto found_person_row(const palette& colours, const found_person& one) {
  return person_pick_row(colours, one, request::start_direct{one.id});
}
using found_person_row_t = decltype(found_person_row(std::declval<const palette&>(), std::declval<const found_person&>()));
}  // namespace mux::ui
