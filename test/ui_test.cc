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
  void message_menu(mux::ui::menu_facts) {}
  void menu_copy_link() {}
  void menu_save() {}
  void menu_react(std::string) {}
  void react(std::string, std::string) {}
  void close_menu() {}
  void menu_reply() {}
  void menu_edit() {}
  void menu_copy() {}
  void menu_delete() {}
  void cancel_compose() {}
  void open_url(std::string) {}
  void load_older(const mux::conversation_id&, std::string) {}
  void load_context(const mux::conversation_id&, std::string) {}
  void load_newer(const mux::conversation_id&, std::string) {}
  void switch_account(std::string) {}
  void submit_message(std::string text) { sent.push_back(std::move(text)); }
  void send_typed() {}
  void resize_sidebar(float) {}
  void not_implemented(std::string) {}
  void message_person(const mux::conversation_id&) {}
  void jump_to_message(std::string) {}
  void open_search() {}
  void close_search() {}
  void search_typed(std::string) {}
  void search_step(bool) {}
  void reply_to(std::string, std::string) {}
  std::vector<std::string> pictures_opened;
  void open_picture(std::string source, std::string, std::string, std::string) { pictures_opened.push_back(std::move(source)); }
  void save_picture(std::string) {}
  void close_picture() {}
  void open_file(std::string, std::string) {}
  void attach_files() {}
  void typing(bool) {}
  void flip_account_typing() {}
  void settings_files() {}
  void flip_strip_metadata() {}
  void flip_rename_pictures() {}
  void close_send_box() {}
  void send_files() {}
  void open_member_info(std::string) {}
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
  void settings_storage() {}
  void change_limit(mux::config::limit_t, bool) {}
  void clear_stored() {}
  void set_theme(mux::config::theme_t) {}
  void set_renderer(mux::config::renderer_t) {}
  void set_accent(mux::config::accent_t) {}
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

// A long chat scrolled with the wheel, a frame at a time: how long update,
// layout and drawing take, printed, and the whole held to a frame of a
// 60 Hz screen. Then a message arriving at the bottom, the same way.
TEST(Timeline, ScrollsALongChatAtSixtyFrames) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  skia::Sp<skia::SkTypeface> face;
  for (const char* family : {"DejaVu Sans", "Noto Sans", "Liberation Sans"})
    if (manager && !face)
      face = manager->matchFamilyStyle(family, skia::SkFontStyle());
  if (face)
    skiff::paint::fonts().setPrimary(face);
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  stub program;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, &program};

  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.com"};
  const mux::conversation_id room{alice, "!room:example.com"};
  mux::model model;
  model.apply(mux::change_t{mux::change::connection_changed{alice, mux::connection::online{}}});
  model.apply(mux::change_t{mux::change::conversation_updated{
      .id = room, .kind = mux::conversation_kind::group{}, .name = "A busy room"}});
  const std::array<std::string_view, 4> said{
      "Short one.",
      "A message of a few words, as most of them are in a chat like this.",
      "A longer message, which wraps over two or three lines in a bubble: it goes on about something at "
      "length, with a link to https://example.com/some/page in it, and then it ends.",
      "Два слова по-русски, и ещё немного текста, чтобы строка перенеслась."};
  const auto start = std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(1'700'000'000'000));
  const auto add = [&](int i) {
    mux::message one;
    one.in = room;
    one.id = std::format("$event{}", i);
    one.sender = i % 3 == 0 ? "@alice:example.com" : (i % 3 == 1 ? "@bob:example.com" : "@carol:example.com");
    one.outgoing = i % 3 == 0;
    one.at = start + std::chrono::minutes(i);
    one.body.plain = std::string(said[static_cast<std::size_t>(i) % said.size()]);
    model.apply(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  };
  constexpr int kMessages = 600;
  for (int i = 0; i < kMessages; ++i)
    add(i);
  auto& screen = window.root().main();
  screen.chosen = room;
  screen.show(model);

  const skia::SkRect viewport = skia::SkRect::MakeWH(1100.0f, 720.0f);
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(1100, 720));
  ASSERT_TRUE(surface);
  double now = 1000.0;
  using clock = std::chrono::steady_clock;
  double updating = 0.0, laying = 0.0, drawing = 0.0;
  const auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
  const auto frame = [&] {
    now += 16.0;
    const auto a = clock::now();
    window.update(now);
    const auto b = clock::now();
    window.layoutIfNeeded(viewport);
    const auto c = clock::now();
    window.draw(surface->getCanvas());
    (void)window.finishFrame();
    const auto d = clock::now();
    updating += ms(b - a);
    laying += ms(c - b);
    drawing += ms(d - c);
  };
  for (int i = 0; i < 30; ++i)
    frame();  // settled at the newest

  scene::InputRouter router;
  const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
  router.setLayers(layers);
  const skia::SkRect list = screen.timeline.bounds();
  ASSERT_FALSE(list.isEmpty());
  updating = laying = drawing = 0.0;
  constexpr int kFrames = 120;
  // Up where there is room above, down where the view is at the top.
  const float before = screen.timeline.current();
  const float ticks = before > 0.0f ? 1.0f : -1.0f;
  for (int i = 0; i < kFrames; ++i) {
    router.pointer(scene::PointerEvent{scene::pointer::scroll{list.centerX(), list.centerY(), 0.0f, ticks}});
    frame();
  }
  const double per_frame = (updating + laying + drawing) / kFrames;
  std::println("scrolling {} messages, per frame: update {:.2f} ms, layout {:.2f} ms, draw {:.2f} ms, all {:.2f} ms",
               kMessages, updating / kFrames, laying / kFrames, drawing / kFrames, per_frame);
  EXPECT_NE(screen.timeline.current(), before) << "the wheel did not scroll the messages";
  EXPECT_LT(per_frame, 16.0) << "a frame of scrolling is longer than a frame of a 60 Hz screen";

  // The wheel's glide let run out: what is read is measured where the view
  // has come to rest, not in the middle of a glide still under way.
  for (int i = 0; i < 5000 && screen.timeline.moving(); ++i)
    frame();
  ASSERT_FALSE(screen.timeline.moving()) << "the wheel's glide did not come to rest";
  // A message at the bottom, while the reader is up in the history.
  updating = laying = drawing = 0.0;
  // What is being read: a message in view, and where it is on the screen.
  auto& bubbles = std::get<0>(std::get<0>(screen.timeline.fChildren).fChildren);
  const skia::SkRect view = screen.timeline.bounds();
  const auto read = std::ranges::find_if(bubbles, [&](const auto& one) {
    return one.bounds().fTop >= view.fTop && one.bounds().fBottom <= view.fBottom;
  });
  ASSERT_NE(read, bubbles.end());
  const std::string reading_id = read->message_id;
  const float reading = read->bounds().fTop;
  add(kMessages);
  const auto shown = clock::now();
  screen.show(model);
  const double showing = ms(clock::now() - shown);
  frame();
  std::println("a new message: show {:.2f} ms, then update {:.2f} ms, layout {:.2f} ms, draw {:.2f} ms", showing,
               updating, laying, drawing);
  EXPECT_LT(showing + updating + laying + drawing, 16.0);
  // The same message where it was: what is read does not move, whatever
  // the list does above it -- the oldest bubble made goes as the newest
  // comes, and the offset follows what is shown.
  const auto again = std::ranges::find(bubbles, reading_id, &mux::ui::message_bubble::message_id);
  ASSERT_NE(again, bubbles.end());
  EXPECT_NEAR(again->bounds().fTop, reading, 1.0f) << "what was read moved when a message came below it";
  skiff::paint::defaultFont() = nullptr;
}

// A picture in a message, pressed: the viewer is asked for, with it.
TEST(Timeline, APicturePressedIsOpened) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, &program};
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.com"};
  const mux::conversation_id room{alice, "!room:example.com"};
  mux::model model;
  model.apply(mux::change_t{mux::change::connection_changed{alice, mux::connection::online{}}});
  model.apply(mux::change_t{mux::change::conversation_updated{.id = room, .name = "Pictures"}});
  mux::message one;
  one.in = room;
  one.id = "$picture";
  one.sender = "@bob:example.com";
  one.attachment = mux::attachment{.kind = mux::attachment_kind::image{}, .source = "mxc://example.com/abc",
                                   .name = "cat.jpg", .mimetype = "image/jpeg", .size = 1000, .width = 800,
                                   .height = 600};
  model.apply(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  auto& screen = window.root().main();
  screen.chosen = room;
  screen.show(model);
  const skia::SkRect viewport = skia::SkRect::MakeWH(1100.0f, 720.0f);
  for (int i = 0; i < 5; ++i) {
    window.update(1000.0 + 16.0 * i);
    window.layoutIfNeeded(viewport);
    (void)window.finishFrame();
  }
  auto& bubbles = std::get<0>(std::get<0>(screen.timeline.fChildren).fChildren);
  ASSERT_EQ(bubbles.size(), 1u);
  ASSERT_TRUE(bubbles.front().body.picture);
  const skia::SkRect picture = bubbles.front().body.picture->bounds();
  ASSERT_FALSE(picture.isEmpty());
  scene::InputRouter router;
  const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
  router.setLayers(layers);
  router.pointer(scene::PointerEvent{scene::pointer::down{picture.centerX(), picture.centerY(), 1}});
  router.pointer(scene::PointerEvent{scene::pointer::up{picture.centerX(), picture.centerY(), 1}});
  ASSERT_EQ(program.pictures_opened.size(), 1u) << "the press never reached the picture's handler";
  EXPECT_EQ(program.pictures_opened.front(), "mxc://example.com/abc");
  skiff::paint::defaultFont() = nullptr;
}

}  // namespace
