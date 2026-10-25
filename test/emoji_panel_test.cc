// SPDX-License-Identifier: AGPL-3.0-only
// The emoji panel as mux makes it -- its groups, cells, preview and search
// (ui::emoji_panel over an emoji_kept) -- wheeled down and dragged, ticked
// as the window ticks it: the list goes the way it is sent and stays where
// a drag lets it go; never back up, never back to its start.
import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.model;
import skiff.bind;
import mux.core;
import mux.ui;
import gtest;
#include "gtest/gtest-macros.h"
namespace {
namespace ui = mux::ui;
namespace scene = skiff::scene;
namespace model = skiff::model;
struct shown_root {
  model::Tracked<int> nothing;
};
using shown_model = model::Model<shown_root, skiff::bind::NoReactions>;

// Every emoji of the groups, in their order: where in the list each is.
std::map<std::string, std::size_t> order_of(const ui::emoji_kept& kept) {
  std::map<std::string, std::size_t> out;
  std::size_t at = 0;
  for (const auto& group : ui::emoji_groups_of(kept, ui::emoji_search{}))
    for (const auto& item : group.items)
      out.try_emplace(item.glyph, at++);
  return out;
}

struct panel_window {
  skia::SkFont font;
  ui::palette colours{};
  ui::emoji_kept kept{};
  shown_model shown{shown_root{}};
  skiff::bind::Binding<shown_model> binding;
  scene::Scene<ui::emoji_panel_t<ui::insert_emoji_into>> tree;
  std::map<std::string, std::size_t> order;
  double at = 0.0;
  static constexpr float kWidth = 345.0f, kHeight = 400.0f;
  panel_window() : tree(made()) {
    skiff::paint::defaultFont() = &font;
    kept.custom_emoji = false;
    order = order_of(kept);
    tree.root().apply({.width = kWidth, .height = kHeight});
    for (int i = 0; i < 5; ++i)
      frame();
  }
  ~panel_window() { skiff::paint::defaultFont() = nullptr; }
  ui::emoji_panel_t<ui::insert_emoji_into> made() { return ui::emoji_panel(colours, kept, ui::insert_emoji_into{}); }
  // A frame as the window makes one: what was pressed and answered
  // delivered, the binding refreshed, ticked, laid out, finished.
  void frame() {
    auto& work = scene::hostWork();
    for (const auto& way : std::exchange(work.pressed, {}))
      (void)skiff::bind::press(tree.root(), shown, way);
    for (const auto& one : std::exchange(work.answers, {}))
      (void)skiff::bind::answer(tree.root(), shown, one);
    binding.refresh(tree.root(), shown);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeWH(kWidth, kHeight));
    (void)tree.finishFrame();
    at += 16.0;
  }
  // Where the list is: the emoji highest in view, by its place among all.
  std::optional<std::size_t> seen() {
    std::optional<std::pair<float, std::size_t>> top;
    for (const auto& one : tree.semanticsTree()) {
      const auto found = order.find(one.fLabel);
      if (found == order.end() || one.fBounds.isEmpty() || one.fBounds.fTop < 70.0f || one.fBounds.fBottom > kHeight - 40.0f)
        continue;
      if (!top || one.fBounds.fTop < top->first || (one.fBounds.fTop == top->first && found->second < top->second))
        top = std::pair{one.fBounds.fTop, found->second};
    }
    return top.transform([](const auto& one) { return one.second; });
  }
};

TEST(EmojiPanel, AWheelTurnedDownTakesTheListDownAndNeverBackUp) {
  panel_window window;
  (void)window.tree.dispatchPointer(scene::pointer::move{170.0f, 250.0f});
  for (int i = 0; i < 40; ++i)
    window.frame();
  const auto first = window.seen();
  ASSERT_TRUE(first.has_value());
  std::size_t last = *first, furthest = *first;
  int went_back = 0;
  for (int notch = 0; notch < 150; ++notch) {
    (void)window.tree.dispatchPointer(scene::pointer::scroll{170.0f, 250.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      window.frame();
      if (const auto now = window.seen()) {
        if (*now + 8 < last)  // more than a row back
          ++went_back;
        last = *now;
        furthest = std::max(furthest, *now);
      }
    }
  }
  std::println("wheeled 150 notches: from emoji {} to {} (furthest {}), went back {} times", *first, last, furthest, went_back);
  EXPECT_EQ(went_back, 0) << "the list went back up while the wheel went down";
  EXPECT_GT(last, *first + 200);
}

TEST(EmojiPanel, ADraggedListStaysWhereTheDragLetsItGo) {
  panel_window window;
  int jumped = 0;
  std::size_t reached = 0;
  for (int drag = 0; drag < 8; ++drag) {
    (void)window.tree.dispatchPointer(scene::pointer::move{170.0f, 340.0f});
    (void)window.tree.dispatchPointer(scene::pointer::down{170.0f, 340.0f, 1});
    window.frame();
    for (int step = 1; step <= 12; ++step) {
      (void)window.tree.dispatchPointer(scene::pointer::move{170.0f, 340.0f - 15.0f * static_cast<float>(step)});
      window.frame();
    }
    const auto before = window.seen();
    (void)window.tree.dispatchPointer(scene::pointer::up{170.0f, 160.0f, 1});
    for (int i = 0; i < 60; ++i)
      window.frame();
    const auto after = window.seen();
    if (before && after && *after + 8 < *before)
      ++jumped;
    if (after)
      reached = *after;
  }
  std::println("dragged 8 times: at emoji {}, went back {} times on release", reached, jumped);
  EXPECT_EQ(jumped, 0) << "the list went back when the drag let it go";
  EXPECT_GT(reached, 100u);
}
}  // namespace
