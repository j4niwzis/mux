// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:context_menu -- A message's menu.
module;
#include "skiff/named.h"
export module mux.ui:context_menu;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.textbox;
import mux.core;
import mux.config;
import mux.logic.emoji;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import mux.ui.messages;
import :conversations;

export import mux.ui.base;
export import :emoji_popup;

export namespace mux::ui {

// Who has seen a message, as tdesktop's menu shows it (chat_helpers.style's
// defaultWhoRead, who_reacted_context_action.cpp): the read ticks 15 in;
// "N Seen", the one reader's name, or "Nobody Viewed", 44 in; up to three
// userpics of 22 at the right, 17 in, each 8 over the next, in a ring of the
// menu's colour. Pointed at, every reader in a list beside the menu: a
// userpic of 30, 13 in, the name 57 in, and under it when they read.
inline constexpr float kSeenHeight = 33.0f, kSeenFace = 22.0f, kSeenOverlap = 8.0f, kSeenRight = 17.0f;
inline constexpr std::size_t kSeenMostFaces = 3;
// The row and the list, pointed at or left: the list open while either is.
struct seen_row_hovered {
  bool on = false;
  skia::SkRect box;
};
struct seen_list_hovered {
  bool on = false;
};
struct seen_row_says {
  using Answer = seen_row_hovered;
  seen_row_hovered operator()(bool on, const skia::SkRect& box) const { return {on, box}; }
};
struct seen_list_says {
  using Answer = seen_list_hovered;
  seen_list_hovered operator()(bool on, const skia::SkRect&) const { return {on}; }
};
inline auto seen_row(const palette& colours, const std::vector<seen_reader>& readers) {
  namespace c = skiff::compose;
  const std::size_t shown = std::min(kSeenMostFaces, readers.size());
  const float faces_width = shown ? kSeenFace + static_cast<float>(shown - 1) * (kSeenFace - kSeenOverlap) : 0.0f;
  // The faces out of the flow, in the room at the right: the first the
  // rightmost, 17 in from the edge, each next 14 to its left.
  auto faces = std::views::iota(std::size_t{0}, shown) | std::views::transform([&](std::size_t i) {
                 return c::styled({.place = scene::anchor::kCentreRight,
                                   .x = faces_width + 8.0f - static_cast<float>(i) * (kSeenFace - kSeenOverlap),
                                   .border = scene::Border{colours.sidebar, 2.0f}},
                                  avatar_mark(avatar_of::person{}, readers[i].id, readers[i].name, kSeenFace));
               }) | std::ranges::to<std::vector>();
  const std::string said = readers.empty() ? std::string("Nobody Viewed")
                           : readers.size() == 1 ? readers.front().name
                                                 : std::to_string(readers.size()) + " Seen";
  return SKIFF_NAMED(seen_row, c::hovering(seen_row_says{},
      c::row(c::hbox(0.0f, {.fillX = true, .height = kSeenHeight,
                            .padding = {9.0f, kSeenRight + (shown ? faces_width + 8.0f : 0.0f), 7.0f, 44.0f}, .hoverBackground = colours.chosen}),
             c::styled({.place = scene::anchor::kCentreLeft, .x = 15.0f - 44.0f}, icon_mark(colours, icon::check{})),
             c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, elided(nodes::Text(said, 13.0f, colours.text))),
             c::many(c::hbox(), std::move(faces)))));
}
// A reader in the list: pressed, the menu closed and their card opened.
inline auto reader_row(const palette& colours, const seen_reader& one) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(reader_row, c::onClick(std::tuple{request::close_menu{}, request::open_member_info{one.id}},
      c::row(c::hbox(14.0f, {.fillX = true, .height = 44.0f, .padding = {6.0f, 17.0f, 6.0f, 13.0f}, .hoverBackground = colours.chosen}),
             avatar_mark(avatar_of::person{}, one.id, one.name, 30.0f),
             c::column(c::vbox(0.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}),
                       elided(nodes::Text(one.name, 13.0f, colours.text)),
                       elided(nodes::Text(one.at ? clock_of(*one.at) : std::string("seen"), 12.0f, colours.dim)))),
      one.name));
}
// The readers, scrolling where there are more than fit: at most about
// seven rows tall.
inline constexpr float kSeenRow = 44.0f, kSeenMostTall = 320.0f, kSeenListWidth = 240.0f;
inline auto seen_list(const palette& colours, const std::vector<seen_reader>& readers) {
  namespace c = skiff::compose;
  const float tall = std::min(kSeenMostTall, kSeenRow * static_cast<float>(readers.size()) + 10.0f);
  // Hovered in a box as big as the list: filling across, as a row's does, it
  // was popped as wide as the window, at its left edge.
  return SKIFF_NAMED(seen_list, c::hovering(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), seen_list_says{},
      c::column(c::vbox(0.0f, {.width = kSeenListWidth, .height = tall, .padding = {6.0f, 0.0f, 4.0f, 0.0f}, .cornerRadius = 10.0f,
                               .background = colours.popup(), .border = scene::Border{colours.band, 1.0f}, .masking = true}),
                c::styled({.fill = true},
                          nodes::ScrollContainer(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                              readers | std::views::transform([&](const seen_reader& one) { return reader_row(colours, one); }) | std::ranges::to<std::vector>()))))));
}

inline auto emoji_favourite_row(const palette& colours, const emoji_kept& kept, const std::optional<emote>& emoji) {
  namespace c = skiff::compose;
  return c::visible(emoji.has_value(), c::onClick(request::favourite_emoji{emoji.value_or(emote{})},
      c::row(c::hbox(0.0f, {.fillX = true, .height = 33.0f, .padding = {0.0f, 17.0f, 0.0f, 17.0f}, .hoverBackground = colours.chosen}),
          c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(emoji && kept.emoji_is_favourite(emoji->url)
              ? "Remove from favorites" : "Add to favorites", 13.0f, colours.text)))));
}
// What the menu's own parts do to what it has open: a top unrolled (its
// place among the plugins' tops), the list of readers pointed at or left.
//
// Each top's unrolling a plain on() of its own, not one template for every
// feature: a debug build routes a press's answer through a table of the
// event types a Local's handlers take, found by offering on() a probe that
// converts to anything (skiff.bind:takes) -- and no conversion deduces a
// template's Feature. menu_unroll<Feature> was in no table, the arrow's
// answer went up past the menu unseen, and the emoji never unrolled (the
// user's: "I can't even open it"). The menu built alone, as a test's root,
// routes the static way, and did.
template <class Feature, std::size_t At>
struct unroll_of {
  auto on(const contract::menu_unroll<Feature>&, const contract::message_menu_state& now) const {
    auto next = now;
    next.unrolled = At;
    return skiff::model::over<contract::message_menu_state>(skiff::model::setTo(std::move(next)));
  }
};
template <class Places, class... Features>
struct menu_events_at;
template <std::size_t... At, class... Features>
struct menu_events_at<std::index_sequence<At...>, Features...> : unroll_of<Features, At + 1>... {
  using unroll_of<Features, At + 1>::on...;
  auto on(const seen_row_hovered& one, const contract::message_menu_state& now) const {
    auto next = now;
    next.seen_row = one.on;
    if (one.on) {
      next.seen_left = one.box.fLeft;
      next.seen_top = one.box.fTop;
      next.seen_right = one.box.fRight;
      next.seen_bottom = one.box.fBottom;
    }
    return skiff::model::over<contract::message_menu_state>(skiff::model::setTo(std::move(next)));
  }
  auto on(const seen_list_hovered& one, const contract::message_menu_state& now) const {
    auto next = now;
    next.seen_list = one.on;
    return skiff::model::over<contract::message_menu_state>(skiff::model::setTo(std::move(next)));
  }
};
template <class List>
struct menu_events_of;
template <class... Features>
struct menu_events_of<plugin_list<Features...>> : menu_events_at<std::index_sequence_for<Features...>, Features...> {
  // Each's place among them, from 1: found by overloading on its tag.
  template <class Feature, std::size_t At> struct slot_entry {
    static constexpr std::size_t at(type_tag<Feature>) { return At; }
  };
  template <class Seq> struct slots;
  template <std::size_t... At> struct slots<std::index_sequence<At...>> : slot_entry<Features, At + 1>... {
    using slot_entry<Features, At + 1>::at...;
  };
  template <class Feature>
  static constexpr std::size_t slot_of() {
    return slots<std::index_sequence_for<Features...>>::at(type_tag<Feature>{});
  }
};
// What plugins put at its top (contract::message_menu_top) -- the quick
// reactions -- each its head, and what it unrolls over the items once it
// asks, made then: in their order.
template <class List>
struct menu_tops;
template <class... Features>
struct menu_tops<plugin_list<Features...>> {
  template <class Feature>
  static auto head_of(const palette& colours, const menu_facts& facts) {
    static_assert(requires { extend(contract::message_menu_top{}, type_tag<Feature>{}, colours, facts); },
                  composition::broken<contract::message_menu_top, Feature>());
    return extend(contract::message_menu_top{}, type_tag<Feature>{}, colours, facts);
  }
  static auto heads(const palette& colours, const menu_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.autoSize = scene::axes::kBoth}), head_of<Features>(colours, facts)...);
  }
  // Each's own, over the items: made once it unrolls, from their top,
  // filling them -- grown where they left too little room for a list.
  template <class Feature>
  static auto over_of(const palette& colours, emoji_kept& kept) {
    namespace c = skiff::compose;
    using made_t = decltype(unrolled(contract::message_menu_top{}, type_tag<Feature>{}, colours, kept));
    constexpr std::size_t slot = menu_events_of<plugin_list<Features...>>::template slot_of<Feature>();
    return c::projected<contract::message_menu_state>(
        [](const contract::message_menu_state& now) { return now.unrolled == slot ? std::optional(true) : std::nullopt; },
        c::mount<made_t, bool>([at = &colours, from = &kept](bool) { return std::tuple{unrolled(contract::message_menu_top{}, type_tag<Feature>{}, *at, *from)}; },
                               {.place = scene::anchor::kTopLeft, .fill = true, .background = colours.sidebar, .masking = true}));
  }
  static auto overs(const palette& colours, emoji_kept& kept) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .fill = true}), over_of<Features>(colours, kept)...);
  }
};
using menu_tops_t = menu_tops<composition::providers<contract::message_menu_top, enabled_plugins>>;
using menu_events = menu_events_of<composition::providers<contract::message_menu_top, enabled_plugins>>;
// Every top's unrolling in the table a debug build routes the menu's
// answers by (skiff.bind:takes): one missing is an arrow that does nothing.
template <class... Features>
consteval bool unrolls_listed(plugin_list<Features...>) {
  using taken = skiff::bind::takes::OnEvents<menu_events, contract::message_menu_state>;
  return (skiff::bind::takes::kIn<contract::menu_unroll<Features>, taken> && ...);
}
static_assert(unrolls_listed(composition::providers<contract::message_menu_top, enabled_plugins>{}),
              "a menu top's unrolling is not in the menu's handlers' table: its arrow would do nothing");
// The rows plugins add to a message's menu (contract::message_menu), after
// Save As, in the plugins' order.
template <class List>
struct menu_rows;
template <class... Features>
struct menu_rows<plugin_list<Features...>> {
  template <class Feature>
  static auto row_of(const palette& colours, const emoji_kept& kept, const menu_facts& facts) {
    static_assert(requires { extend(contract::message_menu{}, type_tag<Feature>{}, colours, kept, facts); },
                  composition::broken<contract::message_menu, Feature>());
    return extend(contract::message_menu{}, type_tag<Feature>{}, colours, kept, facts);
  }
  static auto rows(const palette& colours, const emoji_kept& kept, const menu_facts& facts) {
    return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row_of<Features>(colours, kept, facts)...);
  }
};
using menu_rows_t = menu_rows<composition::providers<contract::message_menu, enabled_plugins>>;
inline constexpr float kMenuListLeast = 220.0f;
// A message's menu, as tdesktop's, in its order: the quick reactions; every
// emoji, in place of the rest once asked for; Reply, Edit, Pin, Copy, Copy
// Message Link, Save As, Forward, Delete; who has seen it at the foot -- how
// many, and their names beside it as it is pointed at. What does not apply
// to the message left out. At the pointer, going down and right -- up where
// it would pass the window's bottom, left where it would pass its right --
// and kept inside the window. Up and Down go through its items, Enter does
// what is lit, Esc and a press off it close it.
inline auto menu_card(const palette& colours, emoji_kept& kept, const menu_facts& facts) {
  namespace c = skiff::compose;
  const auto item = [&](bool shown, std::string label, auto act, icon_t mark) {
    return c::visible(shown, row_item(colours, std::move(label), std::move(act), mark, std::nullopt, menu_row_look));
  };
  auto items = c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      item(facts.answerable, "Reply", sends<request::menu_reply>{}, icon::back{}),
      item(facts.answerable && facts.can.threads && !facts.link.empty(), "Reply in thread", sends<request::menu_thread>{}, icon::threads{}),
      item(facts.answerable && facts.selection && !facts.own && !facts.copied.empty(), "Quote & Reply", sends<request::menu_quote_reply>{}, icon::back{}),
      item(facts.editable && !facts.reaction && ((!facts.text.empty() && !facts.media) || facts.captioned), "Edit", sends<request::menu_edit>{},
           icon::sliders{}),
      item(!facts.copied.empty(), facts.selection ? "Copy Selected Text" : "Copy Text", sends<request::menu_copy>{}, icon::clip{}),
      item(!facts.link.empty(), "Copy Message Link", sends<request::menu_copy_link>{}, icon::info{}),
      item(!facts.pressed_link.empty(), "Copy Link", sends<request::menu_copy_url>{}, icon::clip{}),
      emoji_favourite_row(colours, kept, facts.emoji),
      item(facts.picture.has_value(), "Copy Image", sends<request::menu_copy_image>{}, icon::clip{}),
      item(facts.media.has_value(), "Save As\u2026", sends<request::menu_save>{}, icon::send{}),
      menu_rows_t::rows(colours, kept, facts),
      item(!facts.id.empty(), "Select", sends<request::menu_select>{}, icon::check{}),
      item(facts.deletable, "Delete", sends<request::menu_delete>{}, icon::close{}),
      c::styled({.fillX = true, .height = 1.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f}}, nodes::Box<>(colours.band)),
      seen_row(colours, facts.seen));
  return SKIFF_NAMED(menu_card, c::onClick(scene::Taken{},
          c::focused_by<keys_asked>(keys_on<keys::menu>{},
          c::moves_focus([](const scene::key::down& press) {
                           return press.key == scene::keys::kEscape ? std::optional(request::close_menu{}) : std::nullopt;
                         },
                         c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f},
                                                  .cornerRadius = 10.0f, .background = colours.popup(), .border = scene::Border{colours.band, 1.0f},
                                                  .shadow = scene::Shadow{skia::colorSetARGB(70, 0, 0, 0), 3.0f}}),
              menu_tops_t::heads(colours, facts),
              // The items, and over them what a top unrolled: the items kept
              // under it, unseen, so the menu keeps its size -- grown only
              // where they left too little room for a list.
              c::spec_for<contract::message_menu_state>([](const contract::message_menu_state& now) {
                return scene::Spec{.minHeight = now.unrolled ? kMenuListLeast : 0.0f};
              },
                  c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                            c::spec_for<contract::message_menu_state>([](const contract::message_menu_state& now) {
                              return scene::Spec{.alpha = now.unrolled ? 0.0f : 1.0f, .disabled = now.unrolled != 0};
                            }, std::move(items)),
                            menu_tops_t::overs(colours, kept))))))));
}
// The menu over the whole window: its card at the pointer, the readers'
// list beside its row while pointed at; a press off them closes it.
inline auto context_menu(const ui_needs& n, const menu_facts& facts) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const skiff::compose::PopAt at{.from = skia::SkRect::MakeXYWH(facts.x, facts.y, 0.0f, 0.0f)};
  const bool readers = !facts.seen.empty();
  // A press off it closes it -- a right press too: caught here, over the
  // whole window, a right-click on another message did nothing at all, the
  // menu left open.
  return SKIFF_NAMED(context_menu, c::local<contract::message_menu_state>(menu_events{},
      c::onRightPress(request::menu_pressed_off{}, c::onClick(request::close_menu{},
          c::column(c::vbox(0.0f, {.fill = true}),
              c::popped_at<contract::message_menu_state>([at](const contract::message_menu_state&) { return std::optional(at); }, 8.0f,
                                                         c::styled({}, menu_card(colours, *n.emoji, facts))),
              c::popped_at<contract::message_menu_state>([readers](const contract::message_menu_state& now) -> std::optional<skiff::compose::PopAt> {
                if (!readers || !(now.seen_row || now.seen_list))
                  return std::nullopt;
                return skiff::compose::PopAt{.from = skia::SkRect::MakeLTRB(now.seen_left, now.seen_top, now.seen_right, now.seen_bottom),
                                             .beside = true, .overlap = 6.0f};
              }, 8.0f, seen_list(colours, facts.seen)))))));
}
using context_menu_t = decltype(context_menu(std::declval<const ui_needs&>(), std::declval<const menu_facts&>()));

}  // namespace mux::ui

