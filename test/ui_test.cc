// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui, driven as the window's user drives it: pressed and typed into.
import std;
import skia;
import skiff.paint;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import mux.protocols;
import mux.ui.proto;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

namespace scene = skiff::scene;

// The program's side, noting only what is sent.
struct stub {
  std::vector<std::string> sent;
  int rooms_joined = 0;
  int invites_declined = 0;
  int room_cards_closed = 0;
  void choose(const mux::conversation_id&) {}
  void send(const mux::conversation_id&, std::string) {}
  void back() {}
  void open_accounts() {}
  void open_new_account() {}
  void add_account_of(mux::protocol_t) {}
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
  void menu_copy_image() {}
  void copy_picture(std::string) {}
  void retry_unsent() {}
  void discard_unsent() {}
  void return_to_chat() {}
  void message_menu(mux::ui::menu_facts) {}
  void menu_copy_link() {}
  void menu_copy_url() {}
  void menu_fave_sticker() {}
  void decline_room_card() { ++invites_declined; }
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
  void jump_to_message(std::string, std::optional<std::string> = std::nullopt, std::optional<std::string> = std::nullopt) {}
  void open_search() {}
  void close_search() {}
  void search_typed(std::string) {}
  void search_step(bool) {}
  void edit_last() {}
  void reply_step(bool) {}
  void reply_to(std::string, std::string) {}
  std::vector<std::string> pictures_opened;
  void open_picture(std::string source, std::string, std::string, std::string) { pictures_opened.push_back(std::move(source)); }
  void save_picture(std::string) {}
  void close_picture() {}
  void open_file(std::string, std::string) {}
  void attach_files() {}
  void typing(bool) {}
  void settings_files() {}
  void flip_strip_metadata() {}
  void flip_show_deleted() {}
  void flip_rename_pictures() {}
  void close_send_box() {}
  void send_files() {}
  void open_member_info(std::string) {}
  void close_notice() {}
  void close_person_info() {}
  void close_room_card() { ++room_cards_closed; }
  void jump_to_mark(mux::mark_kind_t) {}
  void list_marks(mux::mark_kind_t) {}
  void go_to_mark(mux::mark_kind_t, std::string) {}
  void close_marks() {}
  void open_explore() {}
  void close_explore() {}
  void search_rooms(std::string, std::string) {}
  void more_rooms(std::string, std::string, std::string) {}
  void search_pick(std::size_t) {}
  void manage_space(std::string) {}
  void flip_forum(std::string) {}
  void close_forum() {}
  void manage_forum() {}
  void explore_space(std::string, std::string = {}) {}
  void join_directory_room(std::string, std::string) {}
  void create_room(std::string, std::string, bool, std::string, bool = true, bool = false,
                   std::optional<mux::conversation_id> = std::nullopt, bool = false, bool = false) {}
  void open_new_room_in(mux::conversation_id, std::string, bool) {}
  void open_leave_space(mux::conversation_id) {}
  void leave_space(mux::conversation_id, std::vector<std::string>) {}
  void close_leave_space() {}
  void find_people(std::string) {}
  void search_elsewhere(std::string) {}
  void open_packs() {}
  void toggle_threads() {}
  void open_wallpaper(mux::choice_level_t) {}
  void close_wallpaper() {}
  void set_bubbles(mux::choice_level_t, std::optional<mux::config::bubble_look>,
                   mux::config::look_part_t = mux::config::look_part::bubbles{}) {}
  void set_window_opacity(int) {}
  void flip_wallpaper_behind() {}
  void set_frost_blur(int) {}
  void place_spaces(std::string, mux::config::space_bar_t, std::vector<mux::config::space_item_t>,
                    std::optional<mux::config::space_bar_t>, std::optional<mux::config::space_item_t>) {}
  void set_space_bars(std::string, mux::config::space_item_t, bool, bool) {}
  void flip_spaces() {}
  void set_home_hides(mux::choice_level_t, std::optional<bool>) {}
  void set_home_direct(mux::choice_level_t, std::optional<bool>) {}
  void flip_top_bar() {}
  void set_wallpaper(mux::choice_level_t, mux::config::wallpaper_pick_t) {}
  void open_thread(std::string) {}
  void close_thread() {}
  void send_in_thread(std::string, std::string, std::optional<std::string>) {}
  void attach_in_thread() {}
  void toggle_thread_emoji() {}
  void flip_live_blur() {}
  void set_account_colour(mux::config::accent_t) {}
  void flip_account_strip() {}
  void open_replacement() {}
  void knock_room_card() {}
  void place_chat(mux::conversation_id, mux::account_id, bool) {}
  void unplace_chat(mux::conversation_id, mux::account_id) {}
  void flip_chat_strip(mux::conversation_id, mux::account_id) {}
  void set_chat_strip_colour(mux::conversation_id, mux::account_id, mux::config::accent_t) {}
  void flip_home_hide(std::string) {}
  void menu_thread() {}
  void open_room_packs() {}
  void close_packs() {}
  void save_pack(mux::emote_pack) {}
  void delete_pack(mux::emote_pack) {}
  void pick_pack_images() {}
  void open_new_room() {}
  void close_new_room() {}
  void copy_text(std::string) {}
  void text_key(scene::Key, bool = false) {}
  void ask_link() {}
  void set_link(std::string, std::string) {}
  void close_link() {}
  void start_call(mux::conversation_id) {}
  void call_chosen() {}
  void dismiss_call() {}
  void menu_edit_history() {}
  void menu_select() {}
  void toggle_selected(std::string) {}
  void selection_forward() {}
  void selection_copy() {}
  void selection_delete() {}
  void selection_cancel() {}
  void accept_call() {}
  void decline_call() {}
  void hang_up() {}
  void mute_call() {}
  void settings_notifications() {}
  void flip_notify(mux::config::notify_flag_t) {}
  void flip_unified_push() {}
  void set_notify_backend(mux::config::notify_backend_t) {}
  void set_notify_choice(mux::choice_level_t, mux::config::notify_setting_t, std::optional<bool>) {}
  void set_chat_notify(mux::config::notify_mode_t) {}
  void set_room_event_kind(mux::choice_level_t, std::optional<mux::room_event_t>, std::optional<bool>) {}
  void set_room_events(mux::choice_level_t, std::optional<bool>, std::optional<mux::config::room_event_kinds>) {}
  void set_receipts_shown(mux::choice_level_t, std::optional<bool>) {}
  void set_link_previews(mux::choice_level_t, std::optional<bool>) {}
  void set_typing_sent(mux::choice_level_t, std::optional<bool>) {}
  void set_previews_direct(mux::choice_level_t, std::optional<bool>) {}
  void give_passphrase(mux::proto::passphrase_for_t, std::string, std::string, std::string, std::string) {}
  void verify_person(mux::conversation_id) {}
  void verify_accept_now() {}
  void verify_cancel_now() {}
  void verify_match() {}
  void verify_mismatch() {}
  void close_verification() {}
  void flip_local_encryption() {}
  void change_passphrase() {}
  void set_jump_search(mux::choice_level_t, std::optional<std::int64_t>) {}
  void press_loader(std::string) {}
  void stop_jump() {}
  void open_video(std::string, std::string, std::string, std::string, std::string) {}
  void join_room_card() { ++rooms_joined; }
  void toggle_emoji() {}
  void close_emoji() {}
  void insert_emoji(std::string, std::string = {}) {}
  void menu_save_gif() {}
  void show_gifs() {}
  void send_gif(std::string) {}
  void send_sticker(mux::emote) {}
  void play_audio(std::string) {}
  void menu_pin() {}
  void menu_reactions() {}
  void close_reactions() {}
  void close_edit_history() {}
  void open_avatar(std::string) {}
  void open_manage() {}
  void menu_forward() {}
  void menu_view_source() {}
  void close_dialog() {}
  void open_new_chat() {}
  void close_new_chat() {}
  void start_direct(std::string) {}
  void start_group(std::string) {}
  void close_forward() {}
  void forward_to(mux::conversation_id) {}
  void flip_room_events() {}
  void flip_account_room_events() {}
  void flip_chat_room_events() {}
  void close_manage() {}
  void room_act(mux::room_action_t) {}
  void resize_info(float) {}
  void choose_new_proxy(int) {}
  void toggle_mute() {}
  void close_account_pages() {}
  void accounts_back() {}
  void account_page(mux::ui::account_page_t) {}
  // A protocol's own request, as its UI asks it.
  template <class Request>
  void ask_for(Request) {}
  void flip_account_receipts() {}
  void flip_only_verified() {}
  void accept_identity(mux::conversation_id) {}
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
  void flip_partial_redraw() {}
  void flip_vsync() {}
  void flip_show_fps() {}
  void menu_quote_reply() {}
  void show_account(std::string) {}
  void flip_flash_redraws() {}
  void set_renderer(mux::config::renderer_t) {}
  void set_accent(mux::config::accent_t) {}
  void leave_chat() {}
  void close_chat() {}
  void toggle_mute_of(mux::conversation_id) {}
  void set_interface_scale(int) {}
};

// What each test's window reads and paints with: kept until it is gone.
struct ui_state {
  mux::ui::palette colours;
  mux::ui::emoji_kept emoji;
  mux::ui::looks_shown looks;
  mux::ui::ui_shared shared;
  mux::ui::mux_paint paint;

  ui_state() {
    paint.looks = &looks;
    paint.colours = &colours;
  }

  mux::ui::ui_needs<stub> needs(stub& program) {
    return {.actions = &program, .colours = &colours, .emoji = &emoji,
            .looks = &looks, .paint = &paint, .shared = &shared};
  }
};

TEST(Composer, TakesWhatIsTypedIntoIt) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};

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
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
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
  const auto& panel = window.root().layer().frame.base().content();
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

TEST(RoomInfo, LongDescriptionsScrollWithoutHidingTheActions) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  skia::Sp<skia::SkTypeface> face;
  for (const char* family : {"DejaVu Sans", "Noto Sans", "Liberation Sans"})
    if (manager && !face)
      face = manager->matchFamilyStyle(family, skia::SkFontStyle());
  ASSERT_TRUE(face);
  skiff::paint::fonts().setPrimary(face);
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  struct clear_font {
    ~clear_font() { skiff::paint::defaultFont() = nullptr; }
  } clear;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  scene::InputRouter router;
  const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
  router.setLayers(layers);
  auto viewport = skia::SkRect::MakeWH(1000.0f, 720.0f);
  double now = 1000.0;
  const auto frame = [&] {
    window.update(now += 16.0);
    window.layoutIfNeeded(viewport);
  };
  const auto click = [&](const auto& node) {
    const auto box = node.bounds();
    router.pointer(scene::PointerEvent{scene::pointer::down{box.centerX(), box.centerY()}});
    router.pointer(scene::PointerEvent{scene::pointer::up{box.centerX(), box.centerY()}});
  };
  mux::room_preview preview{.id = "!room:example.com", .name = "A room with a long description"};
  for (int i = 0; i < 100; ++i)
    preview.topic += "A long paragraph about this room, with words that wrap on a narrow screen.\n\n";

  for (const bool invite : {false, true}) {
    preview.invite = invite;
    window.root().open_room_card(preview.id, preview);
    auto* card = window.root().layer().room.shown();
    ASSERT_NE(card, nullptr);
    auto& scroll = card->parts.scroll;
    auto& details = std::get<0>(scroll.fChildren);
    // Resize the same open card through desktop, portrait and landscape.
    for (const auto size : {skia::SkRect::MakeWH(1000.0f, 720.0f), skia::SkRect::MakeWH(360.0f, 640.0f),
                            skia::SkRect::MakeWH(640.0f, 360.0f)}) {
      viewport = size;
      for (int i = 0; i < 30; ++i)
        frame();
      EXPECT_TRUE(viewport.contains(card->bounds()));
      EXPECT_GT(scroll.bounds().height(), 0.0f);
      EXPECT_GT(scroll.extent(), scroll.bounds().height());
      EXPECT_GE(scroll.bounds().fTop, card->parts.top.bounds().fBottom);
      EXPECT_LE(scroll.bounds().fBottom, card->parts.join.bounds().fTop);
      EXPECT_TRUE(card->bounds().contains(card->parts.join.bounds()));
      if (invite) {
        ASSERT_TRUE(card->parts.decline);
        EXPECT_TRUE(card->bounds().contains(card->parts.decline->bounds()));
      }

      scroll.setCurrent(0.0f);
      frame();
      const auto top = card->parts.top.bounds();
      const auto join = card->parts.join.bounds();
      const auto view = scroll.bounds();
      // Start over the selectable description, even in a short window.
      scroll.setCurrent(details.parts.about.bounds().fTop - view.fTop);
      frame();
      const float before = scroll.current();
      router.pointer(scene::PointerEvent{scene::pointer::scroll{view.centerX(), view.fTop + 8.0f, 0.0f, -1.0f}});
      for (int i = 0; i < 120; ++i)
        frame();
      EXPECT_GT(scroll.current(), before) << "the wheel over the description did not scroll";
      EXPECT_EQ(card->parts.top.bounds(), top);
      EXPECT_EQ(card->parts.join.bounds(), join);

      scroll.scrollToEnd(false);
      frame();
      EXPECT_NEAR(scroll.current(), scroll.extent(), 1.0f);
      EXPECT_LE(details.parts.id.bounds().fBottom - scroll.current(), view.fBottom + 1.0f);
      EXPECT_GE(details.parts.id.bounds().fTop - scroll.current(), view.fTop - 1.0f);
      const int joined = program.rooms_joined;
      click(card->parts.join);
      EXPECT_EQ(program.rooms_joined, joined + 1);
      if (invite) {
        const int declined = program.invites_declined;
        click(*card->parts.decline);
        EXPECT_EQ(program.invites_declined, declined + 1);
      }
    }
    const int closed = program.room_cards_closed;
    click(card->parts.top.parts.close);
    EXPECT_EQ(program.room_cards_closed, closed + 1);
  }

  // A new, short preview must not inherit the old offset or a tall viewport.
  viewport = skia::SkRect::MakeWH(1000.0f, 720.0f);
  preview.topic = "A brief description.";
  preview.invite = false;
  window.root().open_room_card(preview.id, preview);
  frame();
  auto* card = window.root().layer().room.shown();
  ASSERT_NE(card, nullptr);
  EXPECT_LT(card->bounds().height(), 500.0f);
  EXPECT_FLOAT_EQ(card->parts.scroll.extent(), 0.0f);
  EXPECT_FLOAT_EQ(card->parts.scroll.current(), 0.0f);
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
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};

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
    window.draw(ui.paint, surface->getCanvas());
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
  // Formatted at run time: clang 23.1.2 (CI's build) crashes instantiating
  // this format string's compile-time checks here.
  const double update_ms = updating / kFrames, layout_ms = laying / kFrames, draw_ms = drawing / kFrames;
  const auto messages = kMessages;
  std::cout << std::vformat("scrolling {} messages, per frame: update {:.2f} ms, layout {:.2f} ms, draw {:.2f} ms, all {:.2f} ms",
                            std::make_format_args(messages, update_ms, layout_ms, draw_ms, per_frame))
            << '\n';
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
  // Through vformat, not std::format: clang 23 crashes now and then on
  // basic_format_string<...>::__handles_ for this list of arguments, where
  // libc++'s format headers are in a unit twice -- `import std`, and the
  // skia module's global fragment (Ganesh's headers include <chrono>).
  // make_format_args makes no basic_format_string.
  std::cout << std::vformat("a new message: show {:.2f} ms, then update {:.2f} ms, layout {:.2f} ms, draw {:.2f} ms\n",
                           std::make_format_args(showing, updating, laying, drawing));
  EXPECT_LT(showing + updating + laying + drawing, 16.0);
  // The same message where it was: what is read does not move, whatever
  // the list does above it -- the oldest bubble made goes as the newest
  // comes, and the offset follows what is shown.
  const auto again = std::ranges::find(bubbles, reading_id, &mux::ui::message_bubble<stub>::message_id);
  ASSERT_NE(again, bubbles.end());
  EXPECT_NEAR(again->bounds().fTop, reading, 1.0f) << "what was read moved when a message came below it";
  skiff::paint::defaultFont() = nullptr;
}

// A short answer to a long message, as tdesktop sets it: the bubble as wide
// as the answer and the quote ask (the quote's line counted up to 240), not
// its widest; the quote spanning it; the answer one line, the time beside it.
TEST(Timeline, AShortReplyToALongMessageIsNarrow) {
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
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.com"};
  const mux::conversation_id room{alice, "!room:example.com"};
  mux::model model;
  model.apply(mux::change_t{mux::change::connection_changed{alice, mux::connection::online{}}});
  model.apply(mux::change_t{mux::change::conversation_updated{.id = room, .name = "Replies"}});
  mux::message asked;
  asked.in = room;
  asked.id = "$long";
  asked.sender = "@bob:example.com";
  asked.body.plain = "A long message about one more feature, which goes on and on, well past the width of a "
                     "reply's line, and further still, so that it has to be cut where it is quoted.";
  model.apply(mux::change_t{mux::change::message_added{.message = std::move(asked)}});
  mux::message answer;
  answer.in = room;
  answer.id = "$short";
  answer.sender = "@carol:example.com";
  answer.body.plain = "where does it get it";
  answer.replies_to = "$long";
  model.apply(mux::change_t{mux::change::message_added{.message = std::move(answer)}});
  auto& screen = window.root().main();
  screen.chosen = room;
  screen.show(model);
  const skia::SkRect viewport = skia::SkRect::MakeWH(1100.0f, 720.0f);
  for (int i = 0; i < 6; ++i) {
    window.update(1000.0 + 16.0 * i);
    window.layoutIfNeeded(viewport);
    (void)window.finishFrame();
  }
  auto& bubbles = std::get<0>(std::get<0>(screen.timeline.fChildren).fChildren);
  ASSERT_EQ(bubbles.size(), 2u);
  const auto& body = bubbles.back().parts.body;
  ASSERT_TRUE(body.parts.quote);
  const skia::SkRect bubble = body.bounds();
  const skia::SkRect quote = body.parts.quote->bounds();
  const skia::SkRect text = body.parts.text.bounds();
  EXPECT_LT(bubble.width(), 360.0f) << "the bubble: " << bubble.width() << " wide, the quote " << quote.width()
                                    << ", the text " << text.width();
  EXPECT_NEAR(quote.width(), bubble.width() - 2.0f * mux::ui::message_bubble<stub>::kPadX, 1.0f) << "the quote spans it";
  EXPECT_LT(text.height(), 24.0f) << "the answer is one line: " << text.height() << " high";
  EXPECT_TRUE(body.parts.inline_time.visible()) << "the time beside the answer";
  skiff::paint::defaultFont() = nullptr;
}

// The input's emoji panel, as tdesktop's: the emoji side by side in rows,
// the list taller than the card, and scrolled by the wheel.
TEST(Emoji, ThePanelHasRowsAndScrolls) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  window.root().open_emoji(700.0f, 650.0f);
  const skia::SkRect viewport = skia::SkRect::MakeWH(1100.0f, 720.0f);
  for (int i = 0; i < 4; ++i) {
    window.update(1000.0 + 16.0 * i);
    window.layoutIfNeeded(viewport);
    (void)window.finishFrame();
  }
  auto& popup = *window.root().layer().emoji;
  auto& panel = popup.parts.card.parts.panel;
  auto& sections = panel.sections();
  ASSERT_FALSE(sections.empty());
  const auto& cells = std::get<0>(sections.front().parts.cells.fChildren);
  ASSERT_GE(cells.size(), 2u);
  EXPECT_GT(cells[1].bounds().fLeft, cells[0].bounds().fLeft)
      << "side by side: the card " << popup.parts.card.bounds().width() << " wide, the list "
      << panel.parts.list.bounds().width() << ", the first group " << sections.front().bounds().width()
      << ", its cells " << sections.front().parts.cells.bounds().width();
  EXPECT_FLOAT_EQ(cells[1].bounds().fTop, cells[0].bounds().fTop);
  EXPECT_GT(panel.parts.list.extent(), 0.0f) << "the list: " << panel.parts.list.bounds().height() << " high";
  skiff::paint::defaultFont() = nullptr;
}

// A one-letter message in a group, as in a screenshot: its bubble
// as wide as its name and its letter ask, not its widest; each part's width
// said where it is not.
TEST(Timeline, AOneLetterMessageIsNarrow) {
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
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.com"};
  const mux::conversation_id room{alice, "!room:example.com"};
  mux::model model;
  model.apply(mux::change_t{mux::change::connection_changed{alice, mux::connection::online{}}});
  model.apply(mux::change_t{mux::change::conversation_updated{
      .id = room, .kind = mux::conversation_kind::group{}, .name = "A group"}});
  mux::message one;
  one.in = room;
  one.id = "$a";
  one.sender = "@mika:example.com";
  one.body.plain = "A";
  model.apply(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  auto& screen = window.root().main();
  screen.chosen = room;
  screen.show(model);
  const skia::SkRect viewport = skia::SkRect::MakeWH(1100.0f, 720.0f);
  for (int i = 0; i < 6; ++i) {
    window.update(1000.0 + 16.0 * i);
    window.layoutIfNeeded(viewport);
    (void)window.finishFrame();
  }
  auto& bubbles = std::get<0>(std::get<0>(screen.timeline.fChildren).fChildren);
  ASSERT_EQ(bubbles.size(), 1u);
  const auto& body = bubbles.front().parts.body;
  const auto width = [](const auto& node) { return node.bounds().width(); };
  EXPECT_LT(body.bounds().width(), 250.0f)
      << "the bubble " << width(body) << " wide: its name " << (body.parts.name ? width(*body.parts.name) : -1.0f)
      << ", its text " << width(body.parts.text) << ", its time " << width(body.parts.time) << " (shown "
      << body.parts.time.visible() << "), the time inside " << width(body.parts.inline_time) << " (shown "
      << body.parts.inline_time.visible() << ")";
  skiff::paint::defaultFont() = nullptr;
}

// A picture in a message, pressed: the viewer is asked for, with it.
TEST(Timeline, APicturePressedIsOpened) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
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
  ASSERT_TRUE(bubbles.front().parts.body.parts.picture);
  const skia::SkRect picture = bubbles.front().parts.body.parts.picture->bounds();
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

TEST(Timeline, MovingTheMadeRangeRequestsItsMedia) {
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  auto& screen = window.root().main();
  std::vector<mux::message> messages(3);
  messages[0].id = "$first";
  messages[1].id = "$middle";
  messages[2].id = "$last";
  screen.set_made(messages, 1, 3);
  EXPECT_TRUE(std::exchange(ui.shared.pictures_due, false));
  screen.set_made(messages, 1, 3);
  EXPECT_FALSE(ui.shared.pictures_due);
  screen.set_made(messages, 0, 2);
  EXPECT_TRUE(ui.shared.pictures_due);
}

TEST(Media, AThumbnailArrivalStopsTheLoader) {
  ui_state ui;
  const std::string source = "local:ui-test-picture";
  mux::ui::picture_view picture(ui.colours, source, 2, 2);
  picture.update(0.0);
  EXPECT_TRUE(picture.parts.loader.visible());
  const std::array<std::uint8_t, 16> pixels{};
  mux::ui::thumbnails().put(source, skia::imageFromRGBA(2, 2, pixels.data()));
  picture.update(16.0);
  EXPECT_FALSE(picture.parts.loader.visible());
  EXPECT_NE(picture.parts.picture.image(), nullptr);
  mux::ui::thumbnails().clear();
}

TEST(Media, AVideoWithoutAThumbnailDoesNotWaitForAnImage) {
  ui_state ui;
  mux::ui::picture_view picture(ui.colours, "mxc://example.com/video-without-thumbnail", 320, 240);
  picture.show_video(5000, false);
  picture.update(16.0);
  ASSERT_TRUE(picture.parts.video);
  EXPECT_FALSE(picture.parts.loader.visible());
  EXPECT_FALSE(picture.wantsTick());
}

TEST(Settings, AppearanceUsesTheRefreshedChoiceOnTheNextFrame) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::settings_dialog<stub>> dialog{std::in_place, ui.needs(program), "none"};
  auto& settings = dialog.root();
  const mux::config::theme_t theme{};
  const mux::config::accent_t accent{};
  const auto frame = [&](double now) {
    dialog.update(now);
    dialog.layoutIfNeeded(skia::SkRect::MakeWH(440.0f, 520.0f));
    (void)dialog.finishFrame();
  };
  settings.show_appearance(theme, accent);
  frame(1000.0);
  frame(1300.0);
  settings.keep_offset(100.0f);
  frame(1316.0);
  const float offset = settings.offset();
  ASSERT_GT(offset, 0.0f);

  // Requests reach the dialog before app::refresh publishes looks_shown.
  settings.show_appearance(theme, accent);
  ui.looks.everywhere.bubbles = mux::config::bubble_look{mux::config::bubbles::translucent{}, 65};
  ui.looks.bubbles_everywhere = *ui.looks.everywhere.bubbles;
  frame(1332.0);
  ASSERT_NE(settings.appearance(), nullptr);
  auto& bubbles = settings.appearance()->parts.looks.parts.bubbles;
  EXPECT_EQ(bubbles.parts.kinds.parts.head.parts.value.text(), "Translucent");
  EXPECT_EQ(bubbles.parts.opacity_label.text(), "Opacity: 65%");
  EXPECT_NEAR(bubbles.parts.opacity.fraction(), 55.0f / 90.0f, 0.001f);
  EXPECT_NEAR(settings.offset(), offset, 1.0f);

  settings.show_appearance(theme, accent);
  ui.looks.everywhere.bubbles = mux::config::bubble_look{mux::config::bubbles::frosted{}, 80};
  ui.looks.bubbles_everywhere = *ui.looks.everywhere.bubbles;
  frame(1348.0);
  EXPECT_EQ(settings.appearance()->parts.looks.parts.bubbles.parts.kinds.parts.head.parts.value.text(), "Frosted");

  // A queued refresh must not reopen a page the user just left.
  settings.show_appearance(theme, accent);
  settings.show_home();
  frame(1364.0);
  EXPECT_EQ(settings.appearance(), nullptr);
  skiff::paint::defaultFont() = nullptr;
}

TEST(Settings, OpeningAndChangingPagesStartsAtTheTop) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  for (const auto viewport : {skia::SkRect::MakeWH(1100.0f, 720.0f), skia::SkRect::MakeWH(390.0f, 360.0f)}) {
    stub program;
    ui_state ui;
    scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
    window.root().open_settings("none");
    auto* settings = window.root().settings_up();
    ASSERT_NE(settings, nullptr);
    double now = 1000.0;
    const auto settle = [&] {
      for (int i = 0; i < 25; ++i) {
        window.update(now += 16.0);
        window.layoutIfNeeded(viewport);
        (void)window.finishFrame();
      }
    };
    settle();
    EXPECT_FLOAT_EQ(settings->offset(), 0.0f);
    settings->show_appearance({}, {});
    settle();
    EXPECT_FLOAT_EQ(settings->offset(), 0.0f);
    settings->keep_offset(150.0f);
    settle();
    EXPECT_GT(settings->offset(), 0.0f);
    settings->show_home();
    settle();
    EXPECT_FLOAT_EQ(settings->offset(), 0.0f);
  }
  skiff::paint::defaultFont() = nullptr;
}

}  // namespace
