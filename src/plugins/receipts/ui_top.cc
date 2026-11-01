// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.ui_top -- Who has seen a message, at its menu's foot
// (contract::message_menu_foot), as tdesktop's menu shows it: a row, and
// every reader in a list beside the menu while the row or the list is
// pointed at. Over mux.ui.lists, whose pieces it names (the avatars
// plugin's faces of the readers).
module;
#include "../../inline_in_release.h"
#include "skiff/named.h"
export module mux.plugin.receipts.ui_top;

import std;
import skia;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.nodes.text;
import mux.composition;
import mux.core;
import mux.ui.base;
import mux.ui.lists;
import mux.plugin.receipts.types;

export namespace mux::plugins::receipts {
using namespace ::mux::ui;

// As tdesktop's (chat_helpers.style's defaultWhoRead,
// who_reacted_context_action.cpp): the read ticks 15 in; "N Seen", the one
// reader's name, or "Nobody Viewed", 44 in; up to three userpics of 22 at
// the right, 17 in, each 8 over the next, in a ring of the menu's colour.
// Pointed at, every reader in a list beside the menu: a userpic of 30, 13
// in, the name 57 in, and under it when they read.
inline constexpr float kSeenHeight = 33.0f, kSeenRight = 17.0f;
// What the foot keeps, around the whole menu: the row and the list pointed
// at or not, and where the row is -- the list opening beside it.
struct seen_shown {
  bool row = false, list = false;
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  friend bool operator==(const seen_shown&, const seen_shown&) = default;
};
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
struct seen_events {
  auto on(const seen_row_hovered& one, const seen_shown& now) const {
    auto next = now;
    next.row = one.on;
    if (one.on) {
      next.left = one.box.fLeft;
      next.top = one.box.fTop;
      next.right = one.box.fRight;
      next.bottom = one.box.fBottom;
    }
    return skiff::model::over<seen_shown>(skiff::model::setTo(std::move(next)));
  }
  auto on(const seen_list_hovered& one, const seen_shown& now) const {
    auto next = now;
    next.list = one.on;
    return skiff::model::over<seen_shown>(skiff::model::setTo(std::move(next)));
  }
};
consteval type_tag<seen_shown> foot_state(contract::message_menu_foot, type_tag<feature>) { return {}; }
inline seen_events foot_events(contract::message_menu_foot, type_tag<feature>) { return {}; }

// The row: how many have seen it, or who; the first readers' faces.
MUX_INLINE_IN_RELEASE auto extend(contract::message_menu_foot, type_tag<feature>, const palette& colours, const menu_facts& facts) {
  namespace c = skiff::compose;
  const auto& readers = facts.seen;
  const std::string said = readers.empty() ? std::string("Nobody Viewed")
                           : readers.size() == 1 ? readers.front().name
                                                 : std::to_string(readers.size()) + " Seen";
  return SKIFF_NAMED(seen_row, c::hovering(seen_row_says{},
      c::row(c::hbox(0.0f, {.fillX = true, .height = kSeenHeight, .padding = {9.0f, kSeenRight, 7.0f, 44.0f}, .hoverBackground = colours.chosen}),
             c::styled({.place = scene::anchor::kCentreLeft, .x = 15.0f - 44.0f}, icon_mark(colours, icon::check{})),
             seen_summary(colours, readers, said))));
}
// A reader in the list: pressed, the menu closed and their card opened.
MUX_INLINE_IN_RELEASE auto reader_row(const palette& colours, const seen_reader& one) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(reader_row, c::onClick(std::tuple{request::close_menu{}, request::open_member_info{one.id}},
      c::row(c::hbox(14.0f, {.fillX = true, .height = 44.0f, .padding = {6.0f, 17.0f, 6.0f, 13.0f}, .hoverBackground = colours.chosen}),
             reader_text(colours, one)),
      one.name));
}
// The readers, scrolling where there are more than fit: at most about
// seven rows tall; beside the row while it or the list is pointed at.
inline constexpr float kSeenRow = 44.0f, kSeenMostTall = 320.0f, kSeenListWidth = 240.0f;
MUX_INLINE_IN_RELEASE auto seen_list(const palette& colours, const std::vector<seen_reader>& readers) {
  namespace c = skiff::compose;
  const float tall = std::min(kSeenMostTall, kSeenRow * static_cast<float>(readers.size()) + 10.0f);
  // Hovered in a box as big as the list: filling across, as a row's does, it
  // was popped as wide as the window, at its left edge.
  return SKIFF_NAMED(seen_list, c::hovering(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), seen_list_says{},
      c::column(c::vbox(0.0f, {.width = kSeenListWidth, .height = tall, .padding = {6.0f, 0.0f, 4.0f, 0.0f}, .cornerRadius = 10.0f,
                               .background = colours.popup(), .border = scene::Border{colours.band, 1.0f}, .masking = true}),
                c::styled({.fill = true},
                          c::scrolled(c::many(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                              readers | std::views::transform([&](const seen_reader& one) { return reader_row(colours, one); }) | std::ranges::to<std::vector>()))))));
}
MUX_INLINE_IN_RELEASE auto foot_popover(contract::message_menu_foot, type_tag<feature>, const palette& colours, const menu_facts& facts) {
  namespace c = skiff::compose;
  const bool readers = !facts.seen.empty();
  return c::popped_at<seen_shown>([readers](const seen_shown& now) -> std::optional<skiff::compose::PopAt> {
    if (!readers || !(now.row || now.list))
      return std::nullopt;
    return skiff::compose::PopAt{.from = skia::SkRect::MakeLTRB(now.left, now.top, now.right, now.bottom), .beside = true, .overlap = 6.0f};
  }, 8.0f, seen_list(colours, facts.seen));
}

}  // namespace mux::plugins::receipts
