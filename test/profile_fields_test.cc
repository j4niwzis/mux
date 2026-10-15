// SPDX-License-Identifier: AGPL-3.0-only
import std;
import skia;
import skiff.scene;
import skiff.model;
import skiff.bind;
import mux.core;
import mux.ui.base;
import gtest;
#include "gtest/gtest-macros.h"
namespace {
namespace ui = mux::ui;
namespace scene = skiff::scene;
namespace model = skiff::model;
struct profile_policy { static constexpr bool banners = false; };
struct shown_root { model::Tracked<ui::account_page_read> page; };
using shown_model = model::Model<shown_root, skiff::bind::NoReactions>;
}
TEST(ProfileFields, TextInputEditsTheNestedDraftWithoutChangingTheReceivedProfile) {
  ui::palette colours{};
  mux::user_profile profile; profile.name = "Alice";
  ui::profile_form_seed seed{.profile = profile};
  shown_model shown(shown_root{});
  skiff::bind::Binding<shown_model> binding;
  const auto revision = shown.revision();
  scene::Scene tree(ui::profile_fields<profile_policy>(colours, seed));
  binding.refresh(tree.root(), shown);
  tree.root().apply({.width = 400.0f, .autoSize = scene::axes::kY});
  tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 800.0f));
  auto& name = std::get<1>(std::get<0>(tree.root().fParts).fParts);
  EXPECT_EQ(name.text(), "Alice");
  tree.focus(name);
  EXPECT_TRUE(tree.dispatchText(scene::text::commit{"!"}));
  auto& work = scene::hostWork();
  for (const auto& path : work.pressed) skiff::bind::press(tree.root(), shown, path);
  for (const auto& answer : work.answers) skiff::bind::answer(tree.root(), shown, answer);
  work.answers.clear(); work.pressed.clear();
  binding.refresh(tree.root(), shown);
  EXPECT_NE(tree.root().fModel.root().edit.name, "Alice");
  EXPECT_EQ(tree.root().fModel.root().edit.name, name.text());
  EXPECT_EQ(tree.root().fModel.root().first->name, "Alice");
  EXPECT_EQ(shown.revision(), revision);
}
