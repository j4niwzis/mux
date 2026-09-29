// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui, driven as the window's user drives it: pressed and typed into.
import std;
import skia;
import skiff.paint;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

namespace scene = skiff::scene;

// The program's side, noting only what is sent.
struct stub {
  std::vector<std::string> sent;
  void choose(const mux::conversation_id&) {}
  void send(const mux::conversation_id&, std::string) {}
  void back() {}
  void open_accounts() {}
  void open_new_account() {}
  void add_xmpp() {}
  void add_matrix() {}
  void select_account(std::string) {}
  void toggle_advanced() {}
  void toggle_plain() {}
  void submit_login() {}
  void flip_enabled(std::string) {}
  void remove_account(std::string) {}
  void open_drawer() {}
  void set_motion(std::string) {}
  void quit() {}
  void open_settings() {}
  void close_settings() {}
  void settings_home() {}
  void settings_animations() {}
  void pop_panel() {}
  void toggle_info() {}
  void jump_to_end() {}
  void load_older(const mux::conversation_id&, std::string) {}
  void switch_account(std::string) {}
  void submit_message(std::string text) { sent.push_back(std::move(text)); }
  void send_typed() {}
  void resize_sidebar(float) {}
  void not_implemented(std::string) {}
  void close_notice() {}
  void resize_info(float) {}
  void choose_new_proxy(int) {}
  void toggle_mute() {}
  void close_account_pages() {}
  void accounts_back() {}
  void account_page(int) {}
  void flip_account_receipts() {}
  void proxy_kind(mux::config::proxy_kind_t) {}
  void choose_account_proxy(int) {}
  void manage_proxies() {}
  void settings_proxies() {}
  void add_proxy() {}
  void edit_proxy(int) {}
  void save_proxy_profile() {}
  void delete_proxy_profile() {}
  void settings_appearance() {}
  void settings_rendering() {}
  void set_theme(mux::config::theme_t) {}
  void set_renderer(mux::config::renderer_t) {}
  void leave_chat() {}
};

TEST(Composer, TakesWhatIsTypedIntoIt) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, &program};

  const mux::account_id alice{mux::protocol::xmpp{}, "alice@example.com"};
  const mux::conversation_id with_bob{alice, "bob@example.com"};
  mux::model model;
  model.apply(mux::change_t{mux::change::connection_changed{alice, mux::connection::online{}}});
  model.apply(mux::change_t{mux::change::conversation_updated{.id = with_bob, .name = "Bob"}});
  auto& screen = window.root().main();
  screen.chosen = with_bob;
  screen.show(model);
  window.layoutIfNeeded(skia::SkRect::MakeWH(1000.0f, 700.0f));

  const skia::SkRect field = screen.line.field.bounds();
  ASSERT_FALSE(field.isEmpty());
  scene::InputRouter router;
  const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
  router.setLayers(layers);
  router.pointer(scene::PointerEvent{scene::pointer::down{field.centerX(), field.centerY()}});
  router.pointer(scene::PointerEvent{scene::pointer::up{field.centerX(), field.centerY()}});
  EXPECT_EQ(window.focusedId(), screen.line.field.id());

  router.text(scene::TextEvent{scene::text::commit{"hello"}});
  EXPECT_EQ(screen.line.text(), "hello");
  router.key(scene::KeyEvent{scene::key::down{scene::keys::kEnter,
                                              scene::Modifiers{}.with<scene::modifier::shift>(true), false}});
  router.text(scene::TextEvent{scene::text::commit{"there"}});
  EXPECT_EQ(screen.line.text(), "hello\nthere");
  router.key(scene::KeyEvent{scene::key::down{scene::keys::kEnter, scene::Modifiers{}, false}});
  ASSERT_EQ(program.sent.size(), 1u);
  EXPECT_EQ(program.sent.front(), "hello\nthere");
  skiff::paint::defaultFont() = nullptr;
}

// The drawer slides back out when closed, however long it was out.
TEST(Drawer, SlidesOutAfterALongWhileOut) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  skiff::paint::motionLevel() = skiff::paint::motion::full{};
  stub program;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, &program};
  mux::model model;
  window.root().main().show(model);
  const skia::SkRect viewport = skia::SkRect::MakeWH(1000.0f, 700.0f);
  double now = 1000.0;
  const auto frame = [&](double at) {
    now = at;
    window.update(now);
    window.layoutIfNeeded(viewport);
    (void)window.finishFrame();
  };
  frame(now);
  window.root().open_drawer();
  for (int i = 0; i < 40; ++i)
    frame(now + 16.0);
  const auto& panel = window.root().p->frame.base().content();
  EXPECT_FLOAT_EQ(panel.bounds().fLeft, 0.0f);

  // A long while with nothing to draw, then a press on the dimmed rest.
  now += 60'000.0;
  scene::InputRouter router;
  const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
  router.setLayers(layers);
  router.pointer(scene::PointerEvent{scene::pointer::down{900.0f, 300.0f}});
  router.pointer(scene::PointerEvent{scene::pointer::up{900.0f, 300.0f}});
  frame(now + 16.0);
  frame(now + 16.0);
  const float first = panel.bounds().fLeft;
  EXPECT_LT(first, 0.0f);
  EXPECT_GT(first, -panel.bounds().width() * 0.5f) << "it jumped instead of sliding";
  for (int i = 0; i < 40; ++i)
    frame(now + 16.0);
  EXPECT_FALSE(panel.visible());
  skiff::paint::defaultFont() = nullptr;
}

}  // namespace
