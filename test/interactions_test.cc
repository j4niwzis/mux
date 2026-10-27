// SPDX-License-Identifier: AGPL-3.0-only
// Exercise pointer routing, rather than calling the actions directly.
import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.model;
import mux.ui;
import mux.plugins.ui;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace scene = skiff::scene;
namespace ui = mux::ui;
struct root {};
using model = skiff::model::Model<root, skiff::bind::NoReactions>;
struct requests {
  std::vector<std::string> threads, reactions;
  std::vector<std::string> captions;
  int closed = 0;
  void take(const ui::request::open_thread& one) { threads.push_back(one.root); }
  void take(const ui::request::menu_react& one) { reactions.push_back(one.key); }
  void take(const ui::request::close_menu&) { ++closed; }
  void take(const ui::request::send_files& one) { captions.push_back(one.caption); }
  template <class E> void take(const E&) { ADD_FAILURE() << "Unexpected application request"; }
};
struct environment {
  skia::SkFont font;
  ui::palette colours;
  ui::ui_shared shared;
  ui::emoji_kept emoji;
  ui::ui_needs needs{.colours = &colours, .emoji = &emoji, .shared = &shared};
  environment() {
    skiff::paint::defaultFont() = &font;
    scene::hostWork().pressed.clear();
    scene::hostWork().answers.clear();
  }
  ~environment() {
    scene::hostWork().pressed.clear();
    scene::hostWork().answers.clear();
    skiff::paint::defaultFont() = nullptr;
  }
};
void click(auto& tree, const skia::SkRect& bounds, model& state, requests& heard) {
  tree.dispatchPointer(scene::pointer::down{bounds.centerX(), bounds.centerY(), 1});
  tree.dispatchPointer(scene::pointer::up{bounds.centerX(), bounds.centerY(), 1});
  for (const auto& path : std::exchange(scene::hostWork().pressed, {}))
    skiff::bind::press(tree.root(), state, path, &heard);
  for (const auto& answer : std::exchange(scene::hostWork().answers, {}))
    skiff::bind::answer(tree.root(), state, answer, &heard);
}
std::optional<skia::SkRect> button(auto& tree, std::string_view label) {
  for (const auto& one : tree.semanticsTree())
    if (one.fLabel == label && !one.fDisabled && !one.fBounds.isEmpty())
      return one.fBounds;
  return std::nullopt;
}
}

TEST(Interactions, ClickingTheThreadSummaryRequestsItsRoot) {
  environment env;
  model state;
  requests heard;
  scene::Scene tree(ui::message_thread_link(env.colours, "$root", "3 replies · latest answer"));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0, 0, 400, 80));
  const auto link = button(tree, "Open thread");
  ASSERT_TRUE(link.has_value());
  click(tree, *link, state, heard);
  EXPECT_EQ(heard.threads, (std::vector<std::string>{"$root"}));
}

TEST(Interactions, TheAttachmentBoxSendsTheExistingDraftAsItsCaption) {
  environment env;
  model state;
  requests heard;
  const ui::send_facts facts{{ui::pending_file{.name = "notes.txt", .size = 12}}, "text written before attaching"};
  scene::Scene tree(ui::send_box(env.needs, facts));
  skiff::bind::Binding<model> binding;
  binding.refresh(tree.root(), state);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0, 0, 500, 500));
  const auto send = button(tree, "Send");
  ASSERT_TRUE(send.has_value());
  click(tree, *send, state, heard);
  EXPECT_EQ(heard.captions, (std::vector<std::string>{facts.caption}));
}

TEST(Interactions, TheMenuArrowOpensEmojiAndAnEmojiRequestsAReaction) {
  environment env;
  model state;
  requests heard;
  ui::menu_facts facts;
  facts.id = "$message";
  facts.can.react = true;
  scene::Scene tree(ui::context_menu(env.needs, facts));
  skiff::bind::Binding<model> binding;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), state);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0, 0, 800, 700));
    (void)tree.finishFrame();
  };
  for (int i = 0; i < 4; ++i) frame(i * 16.0);
  const auto arrow = button(tree, "All emoji");
  ASSERT_TRUE(arrow.has_value());
  click(tree, *arrow, state, heard);
  for (int i = 4; i < 8; ++i) frame(i * 16.0);
  EXPECT_EQ(heard.closed, 0);
  EXPECT_FALSE(button(tree, "All emoji").has_value());
  const auto face = button(tree, "😀");
  ASSERT_TRUE(face.has_value());
  click(tree, *face, state, heard);
  EXPECT_EQ(heard.reactions, (std::vector<std::string>{"😀"}));
}
