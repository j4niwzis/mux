// SPDX-License-Identifier: AGPL-3.0-only
import std;
import skia;
import skiff.paint;
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
// The name's field, as the scene says it: a text box, by its value -- the
// view's own parts are erased outside a release build, and not reached.
std::optional<scene::Semantics> text_box_valued(auto& tree, std::string_view value) {
  for (const auto& one : tree.semanticsTree())
    if (scene::isTextBox(one.fRole) && one.fValue == value && !one.fBounds.isEmpty())
      return one;
  return std::nullopt;
}
TEST(ProfileFields, TextInputEditsTheNestedDraftWithoutChangingTheReceivedProfile) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
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
  const auto name = text_box_valued(tree, "Alice");
  ASSERT_TRUE(name.has_value());
  // Pressed, it takes the keys; what is typed goes into the draft.
  tree.dispatchPointer(scene::pointer::down{name->fBounds.centerX(), name->fBounds.centerY(), 1});
  tree.dispatchPointer(scene::pointer::up{name->fBounds.centerX(), name->fBounds.centerY(), 1});
  EXPECT_TRUE(tree.dispatchText(scene::text::commit{"!"}));
  auto& work = scene::hostWork();
  for (const auto& path : work.pressed) skiff::bind::press(tree.root(), shown, path);
  for (const auto& answer : work.answers) skiff::bind::answer(tree.root(), shown, answer);
  work.answers.clear(); work.pressed.clear();
  binding.refresh(tree.root(), shown);
  tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 800.0f));
  // The field shows what was typed; the profile as received is not changed:
  // nothing of what is shown was written.
  EXPECT_FALSE(text_box_valued(tree, "Alice").has_value());
  EXPECT_TRUE(std::ranges::any_of(tree.semanticsTree(), [](const scene::Semantics& one) {
    return scene::isTextBox(one.fRole) && one.fValue.starts_with("Alice") && one.fValue.contains('!');
  }));
  EXPECT_EQ(shown.revision(), revision);
  skiff::paint::defaultFont() = nullptr;
}
