// SPDX-License-Identifier: AGPL-3.0-only
import std;
import splice;
import skia;
import skiff.scene;
import skiff.model;
import skiff.bind;
import mux.core;
import mux.config;
import mux.ui;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace scene = skiff::scene;
namespace model = skiff::model;
namespace ui = mux::ui;
struct shown_root {
  model::Tracked<ui::list_shown> list;
  model::Tracked<ui::side_shown> side;
  model::Tracked<ui::listing_shown> listing;
  model::Tracked<ui::found_shown> found;
  model::Tracked<ui::elsewhere_shown> elsewhere;
  model::Tracked<ui::screen_shown> screen;
  model::Tracked<ui::connection_shown> connection;
};
using shown_model = model::Model<shown_root, skiff::bind::NoReactions>;
auto& surface(auto& node) { return std::get<0>(node.fParts); }
auto& rest(auto& node) { return std::get<1>(std::get<2>(surface(node).fParts).fParts); }
auto& chat_scroll(auto& node) { return std::get<0>(std::get<4>(rest(node).fParts).fParts).view(); }
const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 600.0f);
shown_root facts(int count) {
  shown_root root;
  auto listing = std::make_shared<ui::chat_listing>();
  for (int i = 0; i < count; ++i) {
    mux::conversation one;
    one.id.id = std::to_string(i);
    one.timeline.push_back(mux::message{});
    if (i % 2) one.forum_topic = "Topic";
    ui::conversation_view view;
    view.name = "Chat " + std::to_string(i);
    listing->chats.push_back({one.id, std::move(view), {}, {}, ui::conversation_height(one)});
    listing->forums.emplace(one.id, std::move(one));
  }
  root.listing.fValue.listing = std::move(listing);
  root.listing.fValue.revision = 1;
  return root;
}
}

TEST(SideColumn, BoundModelsReachVirtualRowsThroughTheLocalAndKeepFullScrollExtent) {
  ui::palette colours{};
  ui::ui_shared shared{};
  const ui::ui_needs needs{.colours = &colours, .shared = &shared};
  shown_model shown(facts(1000));
  mux::chats_model chats(mux::chats_root{});
  skiff::bind::Binding<shown_model> shown_binding;
  skiff::bind::Binding<mux::chats_model> chats_binding;
  scene::Scene tree(ui::side_column(needs));
  shown_binding.refresh(tree.root(), shown);
  chats_binding.refresh(tree.root(), chats);
  tree.layoutIfNeeded(viewport);
  auto& list = chat_scroll(tree.root());
  ASSERT_FALSE(std::get<0>(list.fChildren).fRows.empty());
  EXPECT_FLOAT_EQ(list.extent(), 72000.0f - list.bounds().height());
  EXPECT_LT(std::get<0>(list.fChildren).fRows.size(), 100u);
  list.setCurrent(50000.0f);
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(list.current(), 50000.0f);
  EXPECT_LE(list.fTops[list.fFrom], 50000.0f);
  EXPECT_GE(list.fTops[list.fTo], 50000.0f + list.bounds().height());
  EXPECT_FLOAT_EQ(tree.root().bounds().width(), 300.0f);
}

TEST(SideColumn, RightPressOpensALocalMenuAndOutsidePressClosesIt) {
  ui::palette colours{};
  ui::ui_shared shared{};
  const ui::ui_needs needs{.colours = &colours, .shared = &shared};
  shown_model shown(facts(1));
  mux::chats_model chats(mux::chats_root{});
  skiff::bind::Binding<shown_model> shown_binding;
  skiff::bind::Binding<mux::chats_model> chats_binding;
  scene::Scene tree(ui::side_column(needs));
  shown_binding.refresh(tree.root(), shown);
  chats_binding.refresh(tree.root(), chats);
  tree.layoutIfNeeded(viewport);
  auto& list = chat_scroll(tree.root());
  ASSERT_EQ(std::get<0>(list.fChildren).fRows.size(), 1u);
  const auto box = list.toView(std::get<0>(list.fChildren).fRows.front().bounds());
  auto& work = scene::hostWork();
  work.answers.clear();
  work.pressed.clear();
  tree.dispatchPointer(scene::pointer::down{box.centerX(), box.centerY(), 3});
  tree.dispatchPointer(scene::pointer::up{box.centerX(), box.centerY(), 3});
  ASSERT_FALSE(work.answers.empty());
  for (const auto& answer : work.answers) skiff::bind::answer(tree.root(), shown, answer);
  work.answers.clear();
  shown_binding.refresh(tree.root(), shown);
  tree.layoutIfNeeded(viewport);
  EXPECT_TRUE(std::get<4>(surface(tree.root()).fParts).visible());
  EXPECT_TRUE(tree.root().fModel.root().chat.has_value());
  tree.dispatchPointer(scene::pointer::down{290.0f, 580.0f, 1});
  tree.dispatchPointer(scene::pointer::up{290.0f, 580.0f, 1});
  for (const auto& answer : work.answers) skiff::bind::answer(tree.root(), shown, answer);
  work.answers.clear();
  work.pressed.clear();
  shown_binding.refresh(tree.root(), shown);
  tree.layoutIfNeeded(viewport);
  EXPECT_FALSE(tree.root().fModel.root().chat.has_value());
}

namespace {
// The side column as the window runs it, frame after frame: both models
// refreshed, ticked, laid out, finished.
struct side_frames {
  ui::palette colours{};
  ui::ui_shared shared{};
  ui::ui_needs needs{.colours = &colours, .shared = &shared};
  shown_model shown{facts(300)};
  mux::chats_model chats{mux::chats_root{}};
  skiff::bind::Binding<shown_model> shown_binding;
  skiff::bind::Binding<mux::chats_model> chats_binding;
  scene::Scene<decltype(ui::side_column(std::declval<const ui::ui_needs&>()))> tree{ui::side_column(needs)};
  double now = 0.0;
  scene::FrameResult frame() {
    now += 16.0;
    shown_binding.refresh(tree.root(), shown);
    chats_binding.refresh(tree.root(), chats);
    tree.update(now);
    tree.layoutIfNeeded(viewport);
    return tree.finishFrame();
  }
};
}

// The rooms standing still, at their top or away from it: no frame asked
// for, nothing laid out. (Away from the top, every frame was asked for and
// laid the list out again, 7 ms each.)
TEST(SideColumn, RoomsStandingStillAskForNoFramesAtTheTopOrAwayFromIt) {
  for (const float at : {0.0f, 3000.0f, 3000.5f}) {
    side_frames side;
    for (int i = 0; i < 5; ++i)
      (void)side.frame();
    auto& list = chat_scroll(side.tree.root());
    list.setCurrent(at);
    for (int i = 0; i < 30; ++i)
      (void)side.frame();
    const float settled = list.current();
    const auto before = scene::walkCounts();
    int wanted = 0;
    for (int i = 0; i < 10; ++i)
      wanted += side.frame().fWantsAnotherFrame ? 1 : 0;
    EXPECT_EQ(wanted, 0) << "at " << at;
    EXPECT_EQ(scene::walkCounts().laidOut - before.laidOut, 0u) << "at " << at;
    EXPECT_FLOAT_EQ(list.current(), settled) << "at " << at;
  }
}

// A room clicked away from the top of the list: its press taken, the list
// where it was.
TEST(SideColumn, ARoomAwayFromTheTopTakesItsClick) {
  for (const float at : {0.0f, 3000.0f}) {
    side_frames side;
    for (int i = 0; i < 5; ++i)
      (void)side.frame();
    auto& list = chat_scroll(side.tree.root());
    list.setCurrent(at);
    for (int i = 0; i < 10; ++i)
      (void)side.frame();
    const auto& rows = std::get<0>(list.fChildren).fRows;
    const auto shown = std::ranges::find_if(rows, [&](const auto& row) { return row.bounds().fTop >= at + 10.0f; });
    ASSERT_NE(shown, rows.end()) << "at " << at;
    const auto box = list.toView(shown->bounds());
    auto& work = scene::hostWork();
    work.answers.clear();
    work.pressed.clear();
    side.tree.dispatchPointer(scene::pointer::move{box.centerX(), box.centerY()});
    (void)side.frame();
    side.tree.dispatchPointer(scene::pointer::down{box.centerX(), box.centerY(), 1});
    (void)side.frame();
    side.tree.dispatchPointer(scene::pointer::up{box.centerX(), box.centerY(), 1});
    (void)side.frame();
    EXPECT_FALSE(work.pressed.empty()) << "at " << at;
    EXPECT_FLOAT_EQ(list.current(), at) << "at " << at;
    work.answers.clear();
    work.pressed.clear();
  }
}
