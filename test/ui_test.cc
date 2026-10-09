// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui, driven as the window's user drives it: pressed and typed into.
import std;
import skia;
import skiff.paint;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.scene;
import skiff.bind;
import skiff.model;
import mux.core;
import mux.config;
import mux.ui;
import mux.protocols;
import mux.logic.room_events;
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
  void set_frost_blur(int) {}
  void place_spaces(std::string, mux::config::space_bar_t, std::vector<mux::config::space_item_t>,
                    std::optional<mux::config::space_bar_t>, std::optional<mux::config::space_item_t>) {}
  void set_space_bars(std::string, mux::config::space_item_t, bool, bool) {}
  void set_home_hides(mux::choice_level_t, std::optional<bool>) {}
  void set_home_direct(mux::choice_level_t, std::optional<bool>) {}
  void set_wallpaper(mux::choice_level_t, mux::config::wallpaper_pick_t) {}
  void open_thread(std::string) {}
  void close_thread() {}
  void send_in_thread(std::string, std::string, std::optional<std::string>) {}
  void attach_in_thread() {}
  void toggle_thread_emoji() {}
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
  void set_chat_notify(mux::config::notify_mode_t) {}
  void give_passphrase(mux::proto::passphrase_for_t, std::string, std::string, std::string, std::string) {}
  void verify_person(mux::conversation_id) {}
  void verify_accept_now() {}
  void verify_cancel_now() {}
  void verify_match() {}
  void verify_mismatch() {}
  void close_verification() {}
  void flip_local_encryption() {}
  void change_passphrase() {}
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
  void flip_account_mentions_shared() {}
  void flip_account_mentions_sealed() {}
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
  void clear_stored() {}
  void menu_quote_reply() {}
  void show_account(std::string) {}
  void set_renderer(mux::config::renderer_t) {}
  void leave_chat() {}
  void close_chat() {}
  void toggle_mute_of(mux::conversation_id) {}
};

// What each test's window reads and paints with: kept until it is gone.
struct ui_state {
  mux::ui::palette colours;
  mux::ui::emoji_kept emoji;
  mux::ui::looks_shown looks;
  mux::ui::ui_shared shared;
  mux::ui::mux_paint paint;
  mux::ui::shown_model showing{mux::ui::shown_root{}};

  template <class Node, class Part> void show(Node &node, Part now) {
    mux::ui::show(showing, std::move(now));
    skiff::bind::refresh(node, showing);
  }

  ui_state() {
    paint.looks = &looks;
    paint.colours = &colours;
  }

  mux::ui::ui_needs<stub> needs(stub&) {
    return {.colours = &colours, .emoji = &emoji,
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
  ui.show(window.root().layer().frame.base(), mux::ui::drawer_shown{true});
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
    ui.show(window.root().layer().room,
            std::optional{mux::ui::room_card_facts{preview.id, preview}});
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
    click(std::get<2>(card->parts.top.fParts));
    EXPECT_EQ(program.room_cards_closed, closed + 1);
  }

  // A new, short preview must not inherit the old offset or a tall viewport.
  viewport = skia::SkRect::MakeWH(1000.0f, 720.0f);
  preview.topic = "A brief description.";
  preview.invite = false;
  ui.show(window.root().layer().room,
          std::optional{mux::ui::room_card_facts{preview.id, preview}});
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

TEST(Timeline, WrappedMessageTimeFollowsTheFinalLineAfterResizing) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
  if (!face) GTEST_SKIP() << "Needs a font for wrapping";
  skiff::paint::fonts().setPrimary(face);
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  struct clear_font { ~clear_font() { skiff::paint::defaultFont() = nullptr; } } clear;
  mux::ui::palette colours;
  mux::ui::looks_shown looks;
  using bubble = mux::ui::message_bubble<stub>;
  scene::Scene<bubble::body_column> view{std::in_place, colours, looks, false,
      "there is maybe one or two people in this room who understand the reference", "04:37"};
  auto& body = view.root();
  body.base_min = 350.0f; // A reply header can make the bubble wider than its last line.
  body.apply({.minWidth = body.base_min});
  double now = 1000.0;
  for (const float width : {440.0f, 260.0f, 440.0f}) {
    for (int frame = 0; frame < 12; ++frame) {
      view.update(now += 16.0);
      view.layoutIfNeeded(skia::SkRect::MakeWH(width, 400.0f));
      (void)view.finishFrame();
    }
    const auto& text = body.parts.text;
    const auto& time = body.parts.inline_time;
    ASSERT_TRUE(time.visible());
    EXPECT_GT(text.bounds().height(), 24.0f);
    EXPECT_NEAR(time.bounds().fBottom, text.bounds().fBottom + bubble::kTimeLower, 0.5f);
    EXPECT_GE(time.bounds().fLeft, text.bounds().fLeft + text.lastLineWidth() + 8.0f);
    EXPECT_FALSE(body.settling());
  }
}

TEST(MessageLinks, RepeatedMessageCardsStayBetweenTheirSurroundingText) {
  const mux::conversation chat{};
  const std::string url = "https://matrix.to/#/!room:example.org/$message";
  const std::string original = "Before " + url + " between " + url + " after";
  const auto shown = mux::ui::with_mentions(original, mux::ui::link_spans_in(original), chat, nullptr);
  ASSERT_EQ(shown.cards.size(), 2u);
  const auto pieces = mux::ui::pieces_of(shown.text, shown.links, shown.styles, shown.cards);
  ASSERT_EQ(pieces.size(), 5u);
  EXPECT_EQ(pieces[0].text, "Before ");
  ASSERT_TRUE(pieces[1].card);
  EXPECT_EQ(pieces[1].card->url, url);
  EXPECT_EQ(pieces[2].text, " between ");
  ASSERT_TRUE(pieces[3].card);
  EXPECT_EQ(pieces[3].card->url, url);
  EXPECT_EQ(pieces[4].text, " after");
}

TEST(MessageLinks, CardsKeepTheirOrderWithMentionsFormattingAndCode) {
  const mux::conversation chat{};
  const std::string first = "https://matrix.to/#/!room:example.org/$first";
  const std::string second = "https://matrix.to/#/!room:example.org/$second";
  auto html = mux::ui::read_html("<a href=\"https://matrix.to/#/@alice:example.org\">Alice</a> before "
      "<a href=\"" + first + "\">first</a> <strong>between</strong><pre>code</pre>"
      "<a href=\"" + second + "\">second</a> after");
  const auto shown = mux::ui::with_mentions(std::move(html.text), std::move(html.spans), chat, nullptr, std::move(html.styles));
  ASSERT_EQ(shown.cards.size(), 2u);
  const auto pieces = mux::ui::pieces_of(shown.text, shown.links, shown.styles, shown.cards);
  ASSERT_EQ(pieces.size(), 7u);
  EXPECT_TRUE(pieces[0].text.ends_with(" before "));
  ASSERT_TRUE(pieces[1].card);
  EXPECT_EQ(pieces[1].card->url, first);
  EXPECT_TRUE(pieces[2].text.contains("between"));
  EXPECT_FALSE(pieces[2].styles.empty());
  EXPECT_TRUE(pieces[3].code);
  EXPECT_EQ(pieces[3].text, "code");
  ASSERT_TRUE(pieces[5].card);
  EXPECT_EQ(pieces[5].card->url, second);
  EXPECT_TRUE(pieces[6].text.ends_with("after"));
}

// The input's emoji panel, as tdesktop's: the emoji side by side in rows,
// the list taller than the card, and scrolled by the wheel.
TEST(Emoji, ThePanelHasRowsAndScrolls) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  ui.show(window.root().layer().emoji,
          std::optional{mux::ui::emoji_facts{700.0f, 650.0f, {}}});
  const skia::SkRect viewport = skia::SkRect::MakeWH(1100.0f, 720.0f);
  for (int i = 0; i < 4; ++i) {
    window.update(1000.0 + 16.0 * i);
    window.layoutIfNeeded(viewport);
    (void)window.finishFrame();
  }
  auto &popup = *window.root().layer().emoji.shown();
  // The transparent outer layer establishes a floating scope. The card
  // must still paint a backdrop, even with a fully transparent tint.
  ui.looks.window.live_blur = true;
  ui.paint.panel = {.active = true, .opacity = 0.0f, .frosted = true,
                    .panels = {ui.colours.sidebar}};
  ui.paint.inside_float = true;
  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(1100, 720));
  pixels.eraseColor(0xff405060u);
  skia::SkCanvas canvas(pixels);
  const auto fill = ui.paint.under(popup.parts.card.fState, ui.colours.sidebar, &canvas, 1.0f);
  EXPECT_TRUE(fill.has_value());
  EXPECT_TRUE(scene::detail::liveBackdrops().contains(popup.parts.card.id()));
  auto& panel = popup.parts.card.parts.panel;
  auto& sections = panel.sections();
  ASSERT_FALSE(sections.empty());
  const auto& cells = mux::ui::cell_section_cells(sections.front());
  ASSERT_GE(cells.size(), 2u);
  EXPECT_GT(cells[1].bounds().fLeft, cells[0].bounds().fLeft)
      << "side by side: the card " << popup.parts.card.bounds().width() << " wide, the list "
      << panel.parts.list.bounds().width() << ", the first group " << sections.front().bounds().width()
      << ", its cells " << std::get<1>(sections.front().fParts).bounds().width();
  EXPECT_FLOAT_EQ(cells[1].bounds().fTop, cells[0].bounds().fTop);
  EXPECT_GT(panel.parts.list.extent(), 0.0f) << "the list: " << panel.parts.list.bounds().height() << " high";
  skiff::paint::defaultFont() = nullptr;
}

TEST(ChatList, LeavingSpacesAndForumsRestoresThePreviousScrollPosition) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct restore_font { ~restore_font() { skiff::paint::defaultFont() = nullptr; } } reset;
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  mux::model model;
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.com"};
  const mux::conversation_id space{alice, "!space:example.com"};
  model.apply(mux::change::connection_changed{alice, mux::connection::online{}});
  std::vector<std::string> children;
  for (int i = 0; i < 100; ++i) {
    const std::string id = "!room" + std::to_string(i) + ":example.com";
    model.apply(mux::change::conversation_updated{.id = {alice, id}, .name = "Room " + std::to_string(i)});
    if (i < 2)
      children.push_back(id);
  }
  model.apply(mux::change::conversation_updated{.id = space, .name = "Space", .space = true, .children = children});
  auto& screen = window.root().main();
  screen.show(model);
  double clock = 1000.0;
  const auto settle = [&] {
    for (int i = 0; i < 40; ++i) {
      window.update(clock += 16.0);
      window.layoutIfNeeded(skia::SkRect::MakeWH(1100.0f, 720.0f));
      (void)window.finishFrame();
    }
  };
  settle();
  EXPECT_FLOAT_EQ(screen.list.current(), 0.0f);
  for (const float position : {0.0f, 3000.0f}) {
    screen.list.setCurrent(position);
    settle();
    ASSERT_NEAR(screen.list.current(), position, 1.0f);
    screen.choose_folder(mux::ui::folder::space{space.id});
    settle();
    EXPECT_FLOAT_EQ(screen.list.current(), 0.0f);
    screen.choose_folder(mux::ui::folder::all{});
    settle();
    EXPECT_NEAR(screen.list.current(), position, 1.0f);
    EXPECT_LT(screen.list.current(), screen.list.extent());
    screen.forums.insert(space);
    screen.show(model);
    settle();
    screen.open_forum(space.id);
    settle();
    EXPECT_FLOAT_EQ(screen.list.current(), 0.0f);
    screen.close_forum();
    settle();
    EXPECT_NEAR(screen.list.current(), position, 1.0f);
    screen.forums.erase(space);
    screen.show(model);
    settle();
  }
}

TEST(Frames, TypingAndScrollingKeepUnchangedPanesOutsideDamage) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  const bool previous_blit = std::exchange(scene::blitScrolling(), true);
  struct restore {
    bool blit;
    ~restore() { scene::blitScrolling() = blit; skiff::paint::defaultFont() = nullptr; }
  } reset{previous_blit};
  stub program;
  ui_state ui;
  scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
  mux::model model;
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.com"};
  const mux::conversation_id room{alice, "!room:example.com"};
  model.apply(mux::change_t{mux::change::connection_changed{alice, mux::connection::online{}}});
  for (int i = 0; i < 80; ++i)
    model.apply(mux::change_t{mux::change::conversation_updated{
        .id = {alice, "!other" + std::to_string(i) + ":example.com"}, .name = "Other room " + std::to_string(i)}});
  model.apply(mux::change_t{mux::change::conversation_updated{.id = room, .name = "Room"}});
  for (int i = 0; i < 80; ++i) {
    mux::message message;
    message.in = room;
    message.id = "$message" + std::to_string(i);
    message.sender = "@bob:example.com";
    message.body.plain = "Message " + std::to_string(i);
    model.apply(mux::change_t{mux::change::message_added{.message = std::move(message)}});
  }
  auto& screen = window.root().main();
  screen.chosen = room;
  screen.show(model);
  double clock = 1000.0;
  const auto frame = [&] {
    window.update(clock += 16.0);
    window.layoutIfNeeded(skia::SkRect::MakeWH(1100.0f, 720.0f));
    return window.finishFrame();
  };
  for (int i = 0; i < 40; ++i)
    (void)frame();
  const auto touches = [](const auto& damage, const skia::SkRect& area) {
    return std::ranges::any_of(damage.fDamageRects, [&](const auto& piece) { return skia::SkRect::Intersects(piece, area); });
  };
  screen.line.set_text("hello");
  EXPECT_FALSE(touches(frame(), screen.list.bounds()));
  screen.line.set_text(":sm");
  EXPECT_FALSE(touches(frame(), screen.list.bounds()));
  screen.timeline.setCurrent(std::max(0.0f, screen.timeline.current() - 100.0f));
  EXPECT_FALSE(touches(frame(), screen.list.bounds()));
  screen.list.setCurrent(100.0f);
  EXPECT_FALSE(touches(frame(), screen.line.field.bounds()));
}

TEST(Emoji, FixedCategoriesStayVisibleWhileCustomPacksArePaged) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct restore_font { ~restore_font() { skiff::paint::defaultFont() = nullptr; } } reset;
  mux::ui::palette colours;
  for (const float width : {180.0f, 320.0f}) {
    mux::ui::emoji_kept kept;
    for (int i = 0; i < 32; ++i) {
      mux::emote emoji;
      emoji.shortcode = "custom" + std::to_string(i);
      emoji.url = "mxc://example.com/" + emoji.shortcode;
      emoji.pack = "Pack " + std::to_string(i);
      emoji.pack_avatar = "mxc://example.com/icon" + std::to_string(i);
      kept.chat_emotes.push_back(std::move(emoji));
    }
    using pick = mux::ui::insert_emoji_into<stub>;
    scene::Scene<mux::ui::emoji_panel<pick>> window{std::in_place, colours, kept, pick{}, scene::Spec{.fill = true}};
    auto& panel = window.root();
    double clock = 1000.0;
    const auto frame = [&] {
      for (int i = 0; i < 4; ++i) {
        window.update(clock += 16.0);
        window.layoutIfNeeded(skia::SkRect::MakeWH(width, 400.0f));
        (void)window.finishFrame();
      }
    };
    frame();
    auto& categories = mux::ui::emoji_category_tabs(panel.parts.footer);
    auto& packs = mux::ui::emoji_pack_tabs(panel.parts.footer);
    ASSERT_GT(categories.size(), 1u);
    ASSERT_EQ(packs.size(), 32u);
    const auto category_row = std::get<0>(panel.parts.footer.fParts).bounds();
    const auto check_categories = [&] {
      for (const auto& tab : categories) {
        EXPECT_TRUE(tab.visible());
        EXPECT_GE(tab.bounds().fLeft, category_row.fLeft - 0.5f);
        EXPECT_LE(tab.bounds().fRight, category_row.fRight + 0.5f);
        EXPECT_NEAR(tab.bounds().centerY(), categories.front().bounds().centerY(), 0.5f);
      }
    };
    const auto check_requested_icons = [&] {
      const auto wanted = panel.pictures_shown();
      for (std::size_t i = 0; i < packs.size(); ++i)
        EXPECT_EQ(std::ranges::contains(wanted, panel.pack_icons[i]), packs[i].visible());
    };
    check_categories();
    check_requested_icons();
    EXPECT_TRUE(packs.front().visible());
    EXPECT_FALSE(packs.back().visible());
    for (std::size_t i = 0; i < packs.size(); ++i) {
      panel.page_packs(true);
      frame();
    }
    EXPECT_TRUE(packs.back().visible());
    EXPECT_FALSE(packs.front().visible());
    check_categories();
    check_requested_icons();
  }
}

TEST(Stickers, PackTabsStayWithinTheFooterAndAllRemainReachable) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct restore_font { ~restore_font() { skiff::paint::defaultFont() = nullptr; } } reset;
  struct panel { void bring(std::size_t) {} } owner;
  mux::ui::palette colours;
  using tab_t = decltype(mux::ui::picker_tab(colours, &owner, 0, std::nullopt));
  using footer_t = decltype(mux::ui::picker_footer<tab_t>());
  for (const float width : {180.0f, 320.0f}) {
    scene::Scene<footer_t> window{std::in_place, mux::ui::picker_footer<tab_t>()};
    auto& footer = window.root();
    auto& tabs = mux::ui::picker_tabs(footer);
    for (std::size_t i = 0; i < 32; ++i)
      tabs.push_back(mux::ui::picker_tab(colours, &owner, i, std::nullopt, "x"));
    footer.invalidateLayout();
    const auto frame = [&](double clock) {
      window.update(clock);
      window.layoutIfNeeded(skia::SkRect::MakeWH(width, 36.0f));
      (void)window.finishFrame();
    };
    for (int i = 0; i < 4; ++i)
      frame(1000.0 + i * 16.0);
    ASSERT_GT(footer.extent(), 0.0f);
    for (const auto& tab : tabs) {
      EXPECT_GE(tab.bounds().fLeft, footer.bounds().fLeft);
      EXPECT_LE(tab.bounds().fRight, footer.bounds().fRight + 0.5f);
    }
    footer.scrollToEnd(false);
    for (int i = 0; i < 4; ++i)
      frame(1100.0 + i * 16.0);
    const auto last = footer.toView(tabs.back().bounds());
    EXPECT_GE(last.fTop, footer.bounds().fTop - 0.5f);
    EXPECT_LE(last.fBottom, footer.bounds().fBottom + 0.5f);
  }
}

namespace {
struct gif_tab_requests {
  bool asked = false;
  void take(const mux::ui::request::show_gifs&) { asked = true; }
  template <class Event> void take(const Event&) { ADD_FAILURE() << "Unexpected request from the GIF tab"; }
};
}

TEST(Emoji, GifTabReceivesPointerPressesOnDesktopAndPhone) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  for (const float width : {1100.0f, 390.0f}) {
    stub program;
    ui_state ui;
    scene::Scene<mux::ui::window<stub>> window{std::in_place, ui.needs(program)};
    ui.show(window.root().layer().emoji,
            std::optional{mux::ui::emoji_facts{width - 30.0f, 650.0f, {}}});
    for (int i = 0; i < 4; ++i) {
      window.update(1000.0 + 16.0 * i);
      window.layoutIfNeeded(skia::SkRect::MakeWH(width, 720.0f));
      (void)window.finishFrame();
    }
    auto& card = window.root().layer().emoji.shown()->parts.card;
    const auto tab = std::get<2>(card.parts.tabs.fParts).bounds();
    ASSERT_FALSE(tab.isEmpty());
    scene::hostWork().pressed.clear();
    scene::InputRouter router;
    const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
    router.setLayers(layers);
    router.pointer(scene::PointerEvent{scene::pointer::down{tab.centerX(), tab.centerY()}});
    router.pointer(scene::PointerEvent{scene::pointer::up{tab.centerX(), tab.centerY()}});
    gif_tab_requests requests;
    const auto presses = std::exchange(scene::hostWork().pressed, {});
    ASSERT_FALSE(presses.empty());
    for (const auto& path : presses)
      ASSERT_TRUE(skiff::bind::press(window.root(), ui.showing, path, &requests));
    EXPECT_TRUE(requests.asked);
    EXPECT_EQ(card.page_at, 2);
    EXPECT_TRUE(card.parts.gifs.visible());
    EXPECT_FALSE(card.parts.panel.visible());
  }
  skiff::paint::defaultFont() = nullptr;
}

TEST(Timeline, SelectedQuoteKeepsAllLinesAndWraps) {
  mux::ui::mentioned shown;
  shown.text = "First quoted line\nSecond quoted line\n\nAnswer";
  shown.styles.push_back({.first = 0, .last = 36, .quote = true});
  const auto quote = mux::ui::take_opening_quote(shown);
  ASSERT_TRUE(quote);
  EXPECT_EQ(*quote, "First quoted line\nSecond quoted line");
  EXPECT_EQ(shown.text, "Answer");
  mux::ui::palette colours;
  auto row = mux::ui::quote_row(colours, colours.accent, "Alice", *quote, std::nullopt, true);
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  scene::Scene<mux::ui::quote_row_t> view{std::move(row)};
  view.layoutIfNeeded(skia::SkRect::MakeWH(200.0f, 400.0f));
  const auto& text = std::get<1>(std::get<2>(view.root().fParts).fParts);
  EXPECT_EQ(text.text(), *quote);
  EXPECT_GT(text.bounds().height(), 26.0f);
  skiff::paint::defaultFont() = nullptr;
}

TEST(Images, AnimatedEmotesTakePriorityOverTheirStillThumbnail) {
  auto still = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  auto moving = skia::Raster(skia::SkImageInfo::MakeN32Premul(16, 16));
  ASSERT_TRUE(still);
  ASSERT_TRUE(moving);
  for (const auto kind : {mux::emote_kind::emoji, mux::emote_kind::sticker}) {
    auto& images = mux::ui::emote_images(kind);
    auto& frames = mux::ui::emote_animations(kind);
    images.put("animation-test", still->makeImageSnapshot());
    frames.put("animation-test", {{moving->makeImageSnapshot(), 100}, {moving->makeImageSnapshot(), 100}});
    const mux::ui::from_emotes source{"animation-test", kind};
    EXPECT_TRUE(source.animated());
    ASSERT_NE(source(), nullptr);
    EXPECT_EQ((*source())->width(), 16);
    skiff::nodes::Image<mux::ui::from_emotes> image(source);
    image.keepBox();
    image.update(0.0);
    EXPECT_TRUE(image.wantsTick());
    EXPECT_TRUE(std::isfinite(image.wakeAt()));
    if (kind == mux::emote_kind::emoji) {
      skiff::nodes::BasicText<mux::ui::message_pictures> text(" ", 14.0f, skia::SkColor{0});
      text.setLinks({{.first = 0, .last = 1, .target = "animation-test", .picture = true}}, skia::SkColor{0});
      text.update(0.0);
      EXPECT_TRUE(text.wantsTick());
      EXPECT_TRUE(std::isfinite(text.wakeAt()));
      frames.clear();
      EXPECT_FALSE(std::isfinite(text.wakeAt()));
      EXPECT_FALSE(text.wantsTick());
    }
    frames.clear();
    images.clear();
  }
}

TEST(Controls, SpaceSwitchReadsEachClickWithoutReplacingTheWidget) {
  using choices = mux::config::chat_choices;
  using model_t = skiff::model::Model<choices, skiff::bind::NoReactions>;
  model_t model(choices{});
  mux::ui::palette colours;
  auto row = mux::ui::space_choice_row<&choices::forum>(colours, "Topics");
  skiff::bind::Binding<model_t> binding;
  binding.refresh(row, model);
  const auto id = std::get<1>(row.fParts).fState.id();
  stub requests;
  for (const bool expected : {true, false, true}) {
    ASSERT_TRUE(skiff::bind::press(row, model, scene::Path{1}, &requests));
    EXPECT_EQ(model.root().forum, expected);
    binding.refresh(row, model);
    EXPECT_EQ(std::get<1>(row.fParts).fState.id(), id);
  }
}

TEST(Images, EmojiAndStickerStoresAreIndependent) {
  auto& emoji = mux::ui::emoji_images();
  auto& stickers = mux::ui::sticker_images();
  emoji.clear();
  stickers.clear();
  auto first = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  auto second = skia::Raster(skia::SkImageInfo::MakeN32Premul(16, 16));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  emoji.put("same-source", first->makeImageSnapshot());
  stickers.put("same-source", second->makeImageSnapshot());
  EXPECT_EQ((*mux::ui::from_emotes{"same-source", mux::emote_kind::emoji}())->width(), 8);
  EXPECT_EQ((*mux::ui::from_emotes{"same-source", mux::emote_kind::sticker}())->width(), 16);
  emoji.clear();
  EXPECT_FALSE(emoji.has("same-source"));
  EXPECT_TRUE(stickers.has("same-source"));
  stickers.clear();
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
  scene::Scene<mux::ui::settings_dialog<stub>> dialog{std::in_place,
                                                      ui.needs(program)};
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
  auto& bubbles = std::get<1>(std::get<1>(settings.appearance()->fParts).fParts).parts.bubbles;
  EXPECT_EQ(bubbles.parts.kinds.parts.head.parts.value.text(), "Translucent");
  EXPECT_EQ(bubbles.parts.opacity_label.text(), "Opacity: 65%");
  EXPECT_NEAR(bubbles.parts.opacity.fraction(), 55.0f / 90.0f, 0.001f);
  EXPECT_NEAR(settings.offset(), offset, 1.0f);

  settings.show_appearance(theme, accent);
  ui.looks.everywhere.bubbles = mux::config::bubble_look{mux::config::bubbles::frosted{}, 80};
  ui.looks.bubbles_everywhere = *ui.looks.everywhere.bubbles;
  frame(1348.0);
  EXPECT_EQ(std::get<1>(std::get<1>(settings.appearance()->fParts).fParts).parts.bubbles.parts.kinds.parts.head.parts.value.text(), "Frosted");

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
    ui.show(window.root().layer().settings,
            std::optional{mux::ui::settings_facts{}});
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

TEST(Calls, PlacementFollowsTheModel) {
  mux::ui::call_shown shown{mux::ui::call_view{}};
  shown.now->phase = mux::ui::call_phase::ringing_in{};
  shown.now->in_view = true;
  EXPECT_TRUE(mux::ui::call_card_of{}(shown).has_value());
  EXPECT_FALSE(mux::ui::call_panel_of{}(shown).has_value());
  shown.now->phase = mux::ui::call_phase::connected{61};
  EXPECT_FALSE(mux::ui::call_card_of{}(shown).has_value());
  EXPECT_TRUE(mux::ui::call_panel_of{}(shown).has_value());
  shown.now->whole = true;
  EXPECT_FALSE(mux::ui::call_card_of{}(shown).has_value());
  EXPECT_FALSE(mux::ui::call_panel_of{}(shown).has_value());
  EXPECT_TRUE(mux::ui::call_screen_of{}(shown).has_value());
  shown.now.reset();
  EXPECT_FALSE(mux::ui::call_screen_of{}(shown).has_value());
}

TEST(Calls, TimerUpdatesTextWithoutRebuildingControls) {
  stub program;
  ui_state ui;
  mux::ui::shown_root root;
  root.call.fValue.now = mux::ui::call_view{};
  root.call.fValue.now->phase = mux::ui::call_phase::connected{61};
  root.call.fValue.now->encrypted = true;
  auto shown = mux::ui::call_layer(
      ui.needs(program), mux::ui::call_card_of{}, mux::ui::call_ui::card{}, {.fill = true});
  mux::ui::shown_model model(std::move(root));
  skiff::bind::Binding<mux::ui::shown_model> binding;
  binding.refresh(shown, model);
  ASSERT_NE(shown.shown(), nullptr);
  const auto id = shown.shown()->fState.id();
  EXPECT_EQ(std::get<1>(std::get<1>(shown.shown()->fParts).fParts).text(), "1:01");
  auto next = *model.look<mux::ui::call_shown>();
  next.now->phase = mux::ui::call_phase::connected{62};
  mux::ui::show(model, std::move(next));
  binding.refresh(shown, model);
  ASSERT_NE(shown.shown(), nullptr);
  EXPECT_EQ(shown.shown()->fState.id(), id);
  EXPECT_EQ(std::get<1>(std::get<1>(shown.shown()->fParts).fParts).text(), "1:02");
}

TEST(Appearance, ThemeCardPressEditsTheFieldAndRefreshesItsRing) {
  struct root { skiff::model::Tracked<mux::config::look_settings> looks; };
  using model_t = skiff::model::Model<root, skiff::bind::NoReactions>;
  using field = skiff::model::Field<&mux::config::look_settings::theme>;
  model_t kept(root{});
  const mux::ui::palette colours;
  const mux::config::theme_t chosen = mux::config::theme::night{};
  auto card = mux::ui::theme_card(colours, chosen, "Night", colours.background, colours.bubble, colours.bubble);
  skiff::bind::Binding<model_t> binding;
  binding.refresh(card, kept);
  auto& preview = std::get<1>(card.fParts);
  ASSERT_TRUE(preview.fState.fBorder.has_value());
  EXPECT_FLOAT_EQ(preview.fState.fBorder->width, 0.0f);
  ASSERT_TRUE(skiff::bind::press(card, kept, scene::Path{}));
  binding.refresh(card, kept);
  EXPECT_EQ(*kept.look<field>(), chosen);
  EXPECT_FLOAT_EQ(preview.fState.fBorder->width, 2.0f);
  EXPECT_EQ(preview.fState.fBorder->colour, colours.accent);
}

TEST(Proxies, DraftValidationPreservesCredentialsAndPortBounds) {
  mux::ui::proxy_draft draft;
  EXPECT_FALSE(mux::ui::proxy_profile(draft));
  draft.name = "Home";
  draft.host = "localhost";
  for (const auto& port : {"", "0", "65536", "1080junk", "-1"}) {
    draft.port = port;
    EXPECT_FALSE(mux::ui::proxy_profile(draft));
  }
  draft.port = "65535";
  draft.password = "secret";
  const auto profile = mux::ui::proxy_profile(draft);
  ASSERT_TRUE(profile);
  EXPECT_EQ(profile->port, 65535);
  EXPECT_EQ(profile->password, "secret");
  EXPECT_FALSE(profile->username.has_value());
}

TEST(Proxies, RequestsCarryTheDraftAndProfileIndex) {
  const mux::ui::proxy_draft draft{.index = 2, .name = "Work", .host = "localhost", .port = "1080"};
  const mux::ui::proxy_draft_events events;
  const auto saved = events.on(mux::ui::save_proxy_draft{}, draft).fEvent;
  ASSERT_TRUE(saved.profile);
  EXPECT_EQ(saved.index, 2);
  EXPECT_EQ(saved.profile->name, "Work");
  EXPECT_EQ(events.on(mux::ui::delete_proxy_draft{}, draft).fEvent.index, 2);
}

TEST(Storage, SealModelRefreshesControlsWithoutReplacingThem) {
  mux::ui::shown_model model(mux::ui::shown_root{});
  mux::ui::palette colours;
  auto view = mux::ui::seal_settings_view(colours);
  skiff::bind::Binding<mux::ui::shown_model> binding;
  binding.refresh(view, model);
  auto& toggle = std::get<1>(std::get<0>(view.fParts).fParts);
  auto& change = std::get<1>(view.fParts);
  const auto id = toggle.fState.id();
  EXPECT_FALSE(toggle.on());
  EXPECT_FALSE(change.visible());
  mux::ui::show(model, mux::ui::local_seal{true});
  binding.refresh(view, model);
  EXPECT_TRUE(toggle.on());
  EXPECT_TRUE(change.visible());
  EXPECT_EQ(toggle.fState.id(), id);
}

TEST(Controls, NameStatusRowsDeclareSelectionAndVisibility) {
  mux::ui::palette colours;
  auto text = mux::ui::two_lines(colours, "Alice", "", 14.0f, 2.0f,
      mux::ui::two_line_style{.first_ink = colours.accent, .selectable = true});
  EXPECT_EQ(std::get<0>(text.fParts).text(), "Alice");
  EXPECT_TRUE(std::get<0>(text.fParts).selectable());
  EXPECT_TRUE(std::get<1>(text.fParts).selectable());
  EXPECT_FALSE(std::get<1>(text.fParts).visible());
  auto forced = mux::ui::two_lines(colours, "Account", "", 14.0f, 2.0f,
      mux::ui::two_line_style{.show_second = true});
  EXPECT_TRUE(std::get<1>(forced.fParts).visible());
}

TEST(Controls, PersonSearchRowSendsItsIdAndNamesItsAction) {
  mux::ui::palette colours;
  const mux::found_person person{.id = "@alice:example.com", .name = "Alice"};
  const auto row = mux::ui::found_person_row(colours, person);
  EXPECT_EQ(row.onPress().user, person.id);
  EXPECT_EQ(row.semantics().fLabel, "Alice");
  EXPECT_EQ(std::get<0>(std::get<1>(row.fParts).fParts).text(), "Alice");
}

TEST(Controls, CopyIdRequestUpdatesOnlyItsLocalFeedback) {
  struct sink {
    std::vector<std::string> copied;
    void take(const mux::ui::request::copy_text& request) { copied.push_back(request.text); }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  auto row = mux::ui::id_line(colours, "#room:example.com", "https://matrix.to/#/#room:example.com");
  skiff::bind::Binding<model_t> binding;
  binding.refresh(row, model);
  auto& label = std::get<1>(row.fParts);
  const auto id = label.fState.id();
  EXPECT_EQ(label.text(), "ID");
  ASSERT_TRUE(skiff::bind::press(row, model, scene::Path{}, &requests));
  binding.refresh(row, model);
  ASSERT_EQ(requests.copied.size(), 1u);
  EXPECT_EQ(requests.copied.front(), "https://matrix.to/#/#room:example.com");
  EXPECT_EQ(label.text(), "ID · link copied, with its servers");
  EXPECT_EQ(label.fState.id(), id);
  EXPECT_EQ(model.root(), 0);
}

TEST(Proxies, LocalEditorPressesSendTypedSaveAndDeleteRequests) {
  struct sink {
    std::optional<mux::ui::request::save_proxy_profile> saved;
    int removed = -1;
    void take(const mux::ui::request::save_proxy_profile& request) { saved = request; }
    void take(const mux::ui::request::delete_proxy_profile& request) { removed = request.index; }
    void take(const mux::ui::request::settings_proxies&) {}
    void take(const mux::ui::request::close_settings&) {}
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  const mux::config::proxy_settings profile{.name = "Home", .host = "localhost", .port = 1080};
  auto editor = mux::ui::proxy_editor(colours, profile, 2);
  skiff::bind::Binding<model_t> binding;
  binding.refresh(editor, model);
  ASSERT_TRUE(skiff::bind::press(editor, model, scene::Path{9, 0}, &requests));
  ASSERT_TRUE(requests.saved.has_value());
  ASSERT_TRUE(requests.saved->profile);
  EXPECT_EQ(requests.saved->index, 2);
  EXPECT_EQ(requests.saved->profile->name, "Home");
  ASSERT_TRUE(skiff::bind::press(editor, model, scene::Path{9, 1}, &requests));
  EXPECT_EQ(requests.removed, 2);
}

TEST(Controls, MemberRowPressSendsItsIdAndKeepsDrawingCached) {
  struct sink {
    std::string opened;
    void take(const mux::ui::request::open_member_info& request) { opened = request.id; }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  const mux::member person{.id = "@alice:example.com", .name = "Alice"};
  auto row = mux::ui::member_row(colours, person, "Online");
  EXPECT_TRUE(row.fState.fRecorded);
  EXPECT_EQ(row.semantics().fLabel, "Alice");
  ASSERT_TRUE(skiff::bind::press(row, model, scene::Path{}, &requests));
  EXPECT_EQ(requests.opened, person.id);
}

TEST(Controls, ActionTileComputesOptionalRequestAtPressTime) {
  struct action {
    using Answer = std::optional<mux::ui::request::not_implemented>;
    const std::string* current;
    Answer operator()() const {
      if (current->empty()) return std::nullopt;
      return mux::ui::request::not_implemented{*current};
    }
  };
  struct sink {
    std::vector<std::string> asked;
    void take(const mux::ui::request::not_implemented& request) { asked.push_back(request.what); }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  std::string current;
  auto tile = mux::ui::action_tile(colours, "Action", mux::ui::icon::info{}, action{&current});
  const auto id = tile.fState.id();
  ASSERT_TRUE(skiff::bind::press(tile, model, scene::Path{}, &requests));
  EXPECT_TRUE(requests.asked.empty());
  current = "Updated action";
  ASSERT_TRUE(skiff::bind::press(tile, model, scene::Path{}, &requests));
  ASSERT_EQ(requests.asked.size(), 1u);
  EXPECT_EQ(requests.asked.front(), current);
  EXPECT_EQ(tile.fState.id(), id);
}

TEST(Forms, LinkEditorSubmitsItsModelFields) {
  struct sink {
    std::optional<mux::ui::request::set_link> saved;
    void take(const mux::ui::request::set_link& request) { saved = request; }
    void take(const mux::ui::request::close_link&) {}
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  auto box = mux::ui::link_box(colours, mux::ui::link_facts{"Original", "https://example.com"});
  box.fModel.apply(skiff::model::over<skiff::model::Field<&mux::ui::link_draft::text>>(
      skiff::model::setTo(std::string("Changed"))));
  skiff::bind::Binding<model_t> binding;
  binding.refresh(box, model);
  ASSERT_TRUE(skiff::bind::press(box, model, scene::Path{3}, &requests));
  ASSERT_TRUE(requests.saved.has_value());
  EXPECT_EQ(requests.saved->text, "Changed");
  EXPECT_EQ(requests.saved->url, "https://example.com");
}

TEST(Forms, ExpressionDialogUsesItsFactoryAndDeclaredLook) {
  stub program;
  ui_state ui;
  const auto needs = ui.needs(program);
  mux::ui::shown_dialog<mux::ui::link_box_t, mux::ui::link_facts, mux::ui::ui_needs<stub>> dialog(&needs);
  mux::ui::look_as_its_content(dialog, *needs.colours);
  dialog.read(std::optional(mux::ui::link_facts{"Text", "https://example.com"}));
  ASSERT_NE(dialog.shown(), nullptr);
  EXPECT_EQ(dialog.shown()->fModel.root().text, "Text");
}

TEST(Forms, PassphraseSubmissionReadsBoundModel) {
  struct sink {
    std::optional<mux::ui::request::give_passphrase> saved;
    void take(const mux::ui::request::give_passphrase& request) { saved = request; }
  } requests;
  mux::ui::shown_model model;
  mux::ui::passphrase_facts facts{mux::config::passphrase_for::change{}, std::nullopt};
  facts.current = "old";
  facts.fresh = facts.again = "new";
  facts.file = "/tmp/keys";
  mux::ui::show(model, std::optional(facts));
  mux::ui::palette colours;
  auto box = mux::ui::passphrase_box(colours);
  skiff::bind::Binding<mux::ui::shown_model> binding;
  binding.refresh(box, model);
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{7}, &requests));
  ASSERT_TRUE(requests.saved.has_value());
  EXPECT_EQ(requests.saved->current, "old");
  EXPECT_EQ(requests.saved->fresh, "new");
  EXPECT_EQ(requests.saved->again, "new");
  EXPECT_EQ(requests.saved->file, "/tmp/keys");
}
TEST(Forms, PassphraseDialogRetainsContentOnModelRefresh) {
  mux::ui::palette colours;
  struct Needs { const mux::ui::palette* colours; } needs{&colours};
  mux::ui::shown_dialog<mux::ui::passphrase_box_t, mux::ui::passphrase_facts, Needs> dialog(&needs);
  mux::ui::look_as_its_content(dialog, colours);
  mux::ui::passphrase_facts facts{mux::config::passphrase_for::unlock{}, std::nullopt};
  dialog.read(std::optional(facts));
  auto* original = dialog.shown();
  ASSERT_NE(original, nullptr);
  facts.refused = "Incorrect passphrase";
  dialog.read(std::optional(facts));
  EXPECT_EQ(dialog.shown(), original);
  EXPECT_FALSE(content_dismissable(std::type_identity<mux::ui::passphrase_box_t>{}, facts));
  facts.why = mux::config::passphrase_for::encrypt{};
  EXPECT_TRUE(content_dismissable(std::type_identity<mux::ui::passphrase_box_t>{}, facts));
}

TEST(Forms, LeaveSpaceChoicesAndRoomsAreModelEdits) {
  struct sink {
    std::optional<mux::ui::request::leave_space> saved;
    bool cancelled = false;
    void take(const mux::ui::request::leave_space& request) { saved = request; }
    void take(const mux::ui::request::close_leave_space&) { cancelled = true; }
  } requests;
  skiff::model::Model<int, skiff::bind::NoReactions> model(0);
  mux::ui::palette colours;
  mux::ui::leave_space_facts facts;
  facts.name = "Space";
  facts.rooms = {{"one", "One"}, {"two", "Two"}};
  auto box = mux::ui::leave_space_box(colours, facts);
  skiff::bind::Binding<decltype(model)> binding;
  binding.refresh(box, model);
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{6, 1}, &requests));
  ASSERT_TRUE(requests.saved.has_value());
  EXPECT_TRUE(requests.saved->rooms.empty());
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{3}, &requests));
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{6, 1}, &requests));
  EXPECT_EQ(requests.saved->rooms, (std::vector<std::string>{"one", "two"}));
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{4}, &requests));
  binding.refresh(box, model);
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{5, 1, 1}, &requests));
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{6, 1}, &requests));
  EXPECT_EQ(requests.saved->rooms, (std::vector<std::string>{"two"}));
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{5, 1, 1}, &requests));
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{6, 1}, &requests));
  EXPECT_TRUE(requests.saved->rooms.empty());
  ASSERT_TRUE(skiff::bind::press(box, model, skiff::scene::Path{6, 0}, &requests));
  EXPECT_TRUE(requests.cancelled);
}

TEST(Emoji, EquallyNamedPacksKeepSeparateSources) {
  const std::vector<mux::emote> images{
      {.shortcode = "one", .url = "mxc://a/one", .pack = "Animals", .pack_room = "!one:a", .pack_key = "a"},
      {.shortcode = "two", .url = "mxc://a/two", .pack = "Animals", .pack_room = "!two:a", .pack_key = "a"},
      {.shortcode = "three", .url = "mxc://a/three", .pack = "Animals", .pack_room = "!one:a", .pack_key = "a"}};
  const auto groups = mux::ui::grouped_emotes(images, "Custom");
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0].second.size(), 2u);
  EXPECT_EQ(groups[1].second.size(), 1u);
  const auto pack = mux::ui::source_pack(groups[0].second, groups[0].first);
  ASSERT_TRUE(pack);
  EXPECT_EQ(pack->chat, "!one:a");
  EXPECT_EQ(pack->key, "a");
  EXPECT_FALSE(mux::ui::source_pack(images, "Recent"));
}

TEST(TextMenu, FormattingAndHistoryFollowTheFieldCapabilities) {
  const auto empty = mux::ui::field_menu_items({.text = false}, false);
  EXPECT_FALSE(empty[0].enabled);
  EXPECT_FALSE(empty[1].enabled);
  EXPECT_FALSE(empty[2].enabled);
  EXPECT_FALSE(empty.back().enabled);
  const auto selected = mux::ui::field_menu_items({.selection = true, .formats = true, .undo = true}, false);
  EXPECT_TRUE(selected[0].enabled);
  EXPECT_TRUE(selected[2].enabled);
  EXPECT_EQ(selected.back().label, "Formatting ›");
  const auto formats = mux::ui::field_menu_items({.selection = true, .formats = true}, true);
  const auto quote = std::ranges::find(formats, "Quote", &mux::ui::text_menu_item::label);
  ASSERT_NE(quote, formats.end());
  EXPECT_TRUE(quote->enabled);
  EXPECT_EQ(quote->shortcut, "Ctrl+Shift+.");
  bool quote_key = false;
  spl::visit(spl::overloaded{[&](const mux::ui::request::text_key& key) {
                              quote_key = key.key == scene::keys::kPeriod && key.shift && key.control;
                            }, [](const auto&) {}}, quote->action);
  EXPECT_TRUE(quote_key);

  const auto masked = mux::ui::field_menu_items({.selection = true, .masked = true, .formats = true}, false);
  EXPECT_FALSE(masked[2].enabled);
  EXPECT_FALSE(masked[3].enabled);
  EXPECT_EQ(masked.back().label, "Select All");
}

TEST(Selection, CapturesTapsAndSelectsAReversedRange) {
  struct row {
    std::string message_id;
    mux::message said;
    skia::SkRect box;
    skia::SkRect bounds() const { return box; }
  };
  struct scroll_view {
    skia::SkRect toView(skia::SkRect box) const { return box; }
  };
  struct area {
    std::vector<row> rows;
    std::set<std::string> selected_ids{"first"};
    std::optional<mux::ui::message_selection_drag> selecting;
    struct { scroll_view timeline; } parts;
    auto& bubbles() { return rows; }
  } area;
  area.rows = {{"first", {}, skia::SkRect::MakeXYWH(0, 0, 100, 40)},
               {"deleted", {.redacted = true}, skia::SkRect::MakeXYWH(0, 40, 100, 40)},
               {"last", {}, skia::SkRect::MakeXYWH(0, 80, 100, 40)}};
  scene::PointerReply press;
  ASSERT_TRUE(mux::ui::selection_down(area, {10, 100, 1}, press));
  EXPECT_TRUE(press.fHandled);
  EXPECT_TRUE(press.fCapturePointer);
  scene::PointerReply move;
  EXPECT_TRUE(mux::ui::selection_move(area, {10, 10}, move));
  scene::PointerReply release;
  const auto chosen = mux::ui::selection_up(area, {10, 10, 1}, release);
  ASSERT_TRUE(chosen);
  EXPECT_EQ(chosen->ids, (std::vector<std::string>{"first", "last"}));
  EXPECT_TRUE(chosen->selected);
  EXPECT_TRUE(release.fReleasePointer);
}


TEST(Selection, TelegramActionsHaveWidthAndDispatchOnDesktopAndPhone) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  for (const float width : {390.0f, 1000.0f}) {
    stub program;
    ui_state ui;
    auto bar = mux::ui::selection_bar(ui.needs(program));
    scene::Scene<decltype(bar)> window{std::move(bar)};
    ui.show(window.root(), mux::ui::selection_shown{2, true, true});
    window.layoutIfNeeded(skia::SkRect::MakeWH(width, 60.0f));
    auto& forward = std::get<0>(window.root().fParts);
    auto& remove = std::get<1>(window.root().fParts);
    auto& clear = std::get<3>(window.root().fParts);
    EXPECT_EQ(std::get<0>(forward.fParts).text(), "Forward 2");
    EXPECT_EQ(std::get<0>(remove.fParts).text(), "Delete 2");
    for (const auto& bounds : {forward.bounds(), remove.bounds(), clear.bounds()}) {
      EXPECT_GT(bounds.width(), 0.0f);
      EXPECT_GT(bounds.height(), 0.0f);
      EXPECT_GE(bounds.fLeft, window.root().bounds().fLeft);
      EXPECT_LE(bounds.fRight, window.root().bounds().fRight);
    }
    scene::hostWork().pressed.clear();
    scene::InputRouter router;
    const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
    router.setLayers(layers);
    struct sink {
      int forwarded = 0, deleted = 0, cleared = 0;
      void take(const mux::ui::request::selection_forward&) { ++forwarded; }
      void take(const mux::ui::request::selection_delete&) { ++deleted; }
      void take(const mux::ui::request::selection_cancel&) { ++cleared; }
    } requests;
    for (const auto& bounds : {forward.bounds(), remove.bounds(), clear.bounds()}) {
      router.pointer(scene::PointerEvent{scene::pointer::down{bounds.centerX(), bounds.centerY()}});
      router.pointer(scene::PointerEvent{scene::pointer::up{bounds.centerX(), bounds.centerY()}});
      const auto presses = std::exchange(scene::hostWork().pressed, {});
      ASSERT_FALSE(presses.empty());
      for (const auto& path : presses)
        ASSERT_TRUE(skiff::bind::press(window.root(), ui.showing, path, &requests));
    }
    EXPECT_EQ(requests.forwarded, 1);
    EXPECT_EQ(requests.deleted, 1);
    EXPECT_EQ(requests.cleared, 1);
    ui.show(window.root(), mux::ui::selection_shown{3, true, false});
    window.layoutIfNeeded(skia::SkRect::MakeWH(width, 60.0f));
    EXPECT_TRUE(forward.visible());
    EXPECT_TRUE(remove.visible());
    EXPECT_TRUE(remove.fState.disabled());
    EXPECT_EQ(std::get<0>(forward.fParts).text(), "Forward 3");
    ui.show(window.root(), mux::ui::selection_shown{});
    EXPECT_FALSE(window.root().visible());
  }
  skiff::paint::defaultFont() = nullptr;
}

TEST(Images, AnimationDeadlinesFollowVariableFrameDurationsAndLoop) {
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  ASSERT_TRUE(surface);
  mux::ui::animation_cache frames;
  frames.put("timed", {{surface->makeImageSnapshot(), 40}, {surface->makeImageSnapshot(), 100}});
  EXPECT_DOUBLE_EQ(frames.next_frame_at("timed", 0.0), 40.0);
  EXPECT_DOUBLE_EQ(frames.next_frame_at("timed", 39.5), 40.0);
  EXPECT_DOUBLE_EQ(frames.next_frame_at("timed", 40.0), 140.0);
  EXPECT_DOUBLE_EQ(frames.next_frame_at("timed", 139.0), 140.0);
  EXPECT_DOUBLE_EQ(frames.next_frame_at("timed", 140.0), 180.0);
  EXPECT_DOUBLE_EQ(frames.next_frame_at("timed", 184.0), 280.0);
  EXPECT_FALSE(std::isfinite(frames.next_frame_at("missing", 10.0)));
  frames.clear();
  EXPECT_FALSE(std::isfinite(frames.next_frame_at("timed", 10.0)));
}

TEST(Images, AnimatedAttachmentDoesNotRequestContinuousSceneAnimation) {
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  ASSERT_TRUE(surface);
  auto& frames = mux::ui::animations();
  frames.put("attachment-timing", {{surface->makeImageSnapshot(), 40}, {surface->makeImageSnapshot(), 100}});
  mux::ui::palette colours;
  mux::ui::picture_view picture(colours, "attachment-timing", 8, 8);
  picture.update(0.0);
  EXPECT_FALSE(picture.settling());
  EXPECT_FALSE(picture.wantsTick());
  EXPECT_TRUE(picture.parts.picture.wantsTick());
  EXPECT_TRUE(std::isfinite(picture.parts.picture.wakeAt()));
  frames.clear();
}

TEST(Images, AvatarSourceChangeInvalidatesCachedImageAndRejectsStaleDownloads) {
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  ASSERT_TRUE(surface);
  mux::ui::avatar_cache cache;
  EXPECT_TRUE(cache.use_source("user", "mxc://old"));
  cache.put("user", surface->makeImageSnapshot());
  EXPECT_FALSE(cache.use_source("user", "mxc://old"));
  EXPECT_TRUE(cache.has("user"));
  EXPECT_TRUE(cache.use_source("user", "mxc://new"));
  EXPECT_FALSE(cache.has("user"));
  EXPECT_FALSE(cache.accepts_source("user", "mxc://old"));
  EXPECT_TRUE(cache.accepts_source("user", "mxc://new"));
  cache.put("user", surface->makeImageSnapshot());
  EXPECT_TRUE(cache.use_source("user", std::nullopt));
  EXPECT_FALSE(cache.has("user"));
  EXPECT_FALSE(cache.accepts_source("user", "mxc://new"));
  EXPECT_FALSE(cache.use_source("user", std::nullopt));
}

TEST(Html, DisclosuresKeepSummaryBodyNestingAndInitialOpenState) {
  const auto read = mux::ui::read_html(
      "<p>Before</p><details><summary><b>More</b></summary><p>Body</p>"
      "<details open><summary>Inner</summary><pre><code>code</code></pre></details></details><p>After</p>");
  ASSERT_EQ(read.disclosures.size(), 2u);
  const auto& outer = read.disclosures[0];
  const auto& inner = read.disclosures[1];
  EXPECT_FALSE(outer.open);
  EXPECT_TRUE(inner.open);
  EXPECT_FALSE(outer.parent);
  EXPECT_EQ(inner.parent, std::optional<std::size_t>{0});
  EXPECT_EQ(read.text.substr(outer.summary_first, outer.summary_last - outer.summary_first), "More");
  EXPECT_EQ(read.text.substr(inner.summary_first, inner.summary_last - inner.summary_first), "Inner");
  EXPECT_LE(outer.first, inner.first);
  EXPECT_GE(outer.last, inner.last);
  EXPECT_TRUE(std::ranges::any_of(read.styles, [](const auto& style) { return style.block && style.code; }));
  const auto fallback = mux::ui::read_html("<details><p>Body without a summary</p></details>");
  ASSERT_EQ(fallback.disclosures.size(), 1u);
  EXPECT_EQ(fallback.disclosures[0].summary_first, fallback.disclosures[0].summary_last);
}

TEST(Html, BlockListsAndCellsKeepTheirBoundaries) {
  const auto read = mux::ui::read_html(
      "before<h4>Heading</h4><ol start=3><li>First</li><li value=9>Second<ul><li>Nested</li></ul></li></ol>"
      "<table><tr><th>A</th><th>B</th></tr><tr><td>C</td><td>D</td></tr></table><script>hidden</script>");
  EXPECT_EQ(read.text, "before\nHeading\n3. First\n9. Second\n  • Nested\nA | B\nC | D");
}

TEST(Html, DisclosuresToggleThroughLocalModelAndKeepChildrenHidden) {
  auto read = mux::ui::read_html("<details><summary>More</summary><p>Body</p>"
      "<details open><summary>Inner</summary>Nested</details></details>");
  mux::ui::mentioned shown;
  shown.text = std::move(read.text);
  shown.links = std::move(read.spans);
  shown.styles = std::move(read.styles);
  shown.disclosures = std::move(read.disclosures);
  mux::ui::palette colours;
  auto view = mux::ui::disclosure_piece(colours, shown, {}, nullptr, colours.accent);
  ui_state ui;
  skiff::bind::refresh(view, ui.showing);
  auto& local = std::get<0>(view.fParts);
  EXPECT_FALSE(local.fModel.root().open[0]);
  EXPECT_TRUE(local.fModel.root().open[1]);
  auto& rows = local.fParts;
  ASSERT_GE(rows.size(), 4u);
  EXPECT_TRUE(spl::visit([](const auto& row) { return row.visible(); }, rows[0]));
  EXPECT_FALSE(spl::visit([](const auto& row) { return row.visible(); }, rows[1]));
  const auto changed = mux::ui::disclosure_events{shown.disclosures}.on(mux::ui::toggle_disclosure{0}, local.fModel.root());
  (void)local.fModel.apply(changed);
  skiff::bind::refresh(view, ui.showing);
  EXPECT_TRUE(local.fModel.root().open[0]);
  EXPECT_TRUE(spl::visit([](const auto& row) { return row.visible(); }, rows[1]));
}

TEST(Html, CustomEmojiCopiesItsLabelAndSourceInsteadOfAnEmptyPlaceholder) {
  const auto read = mux::ui::read_html("Hi <img data-mx-emoticon src=\"mxc://example/emoji\" alt=\":party:\">!");
  ASSERT_EQ(read.spans.size(), 1u);
  EXPECT_EQ(read.spans[0].plain, ":party:");
  skiff::nodes::BasicText<mux::ui::message_pictures> text(read.text, 13.0f, skia::SkColor{0});
  text.setLinks(read.spans, skia::SkColor{0});
  const auto all = text.copiedRange(0, read.text.size());
  EXPECT_EQ(all.text, "Hi :party:!");
  ASSERT_EQ(all.atoms.size(), 1u);
  EXPECT_EQ(all.atoms[0].target, "mxc://example/emoji");
  const auto emoji = text.copiedRange(read.spans[0].first, read.spans[0].last);
  EXPECT_EQ(emoji.text, ":party:");
  ASSERT_EQ(emoji.atoms.size(), 1u);
  EXPECT_EQ(emoji.atoms[0].first, 0u);
}

TEST(Html, MixedReactionTextKeepsWordsAndResolvesEveryCustomEmoji) {
  const std::vector<mux::emote> emojis{{.shortcode = "party", .url = "mxc://example/party"}};
  const auto shown = mux::ui::emoji_text_of("a reaction :party: and :unknown: mxc://example/other", emojis);
  EXPECT_EQ(shown.text, "a reaction \u2003 and :unknown: \u2003");
  ASSERT_EQ(shown.spans.size(), 2u);
  EXPECT_EQ(shown.spans[0].target, "mxc://example/party");
  EXPECT_EQ(shown.spans[0].plain, ":party:");
  EXPECT_EQ(shown.spans[1].target, "mxc://example/other");
}

TEST(About, DependencyPagesUseUniqueResolvedIds) {
  std::set<std::string_view> ids;
  ASSERT_FALSE(mux::ui::about_data::libraries.empty());
  ASSERT_FALSE(mux::ui::about_data::primary_libraries.empty());
  std::set<std::string_view> primary;
  for (const auto id : mux::ui::about_data::primary_libraries) {
    EXPECT_TRUE(primary.insert(id).second);
    EXPECT_NE(mux::ui::library_of(id), nullptr);
    EXPECT_FALSE(id.starts_with("rust:")); // Crates belong on their library's dependency pages.
  }
  EXPECT_TRUE(primary.contains("splice"));
  for (const auto& library : mux::ui::about_data::libraries) {
    EXPECT_TRUE(ids.insert(library.id).second);
    EXPECT_FALSE(library.version.empty());
    EXPECT_FALSE(library.license.empty());
    for (const auto dep : library.dependencies)
      EXPECT_NE(mux::ui::library_of(dep), nullptr) << library.id << " -> " << dep;
  }
}

TEST(Composer, ShortcodeSuggestionInsertsCustomEmojiWithItsSource) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct clear_font { ~clear_font() { skiff::paint::defaultFont() = nullptr; } } clear;
  stub program;
  ui_state ui;
  auto needs = ui.needs(program);
  mux::ui::conversations_screen<stub> screen(needs);
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.org"};
  const mux::conversation_id room{alice, "!room:example.org"};
  mux::model model;
  model.apply(mux::change::conversation_updated{.id = room,
      .emotes = {{.shortcode = "party", .url = "mxc://example/party", .body = "Party"}}});
  screen.chosen = room;
  screen.show(model);
  screen.line.field.setText("Hi :par");
  screen.find_emoji();
  ASSERT_FALSE(screen.emoji_matches.empty());
  EXPECT_EQ(screen.emoji_matches[0].picture, "mxc://example/party");
  screen.choose_emoji(0);
  EXPECT_EQ(screen.line.field.plainText(), "Hi :party:");
  ASSERT_EQ(screen.line.field.atoms().size(), 1u);
  EXPECT_EQ(screen.line.field.atoms()[0].target, "mxc://example/party");
}

TEST(Reactions, MixedCustomEmojiKeepTheOriginalReactionKeyWhenClicked) {
  mux::ui::palette colours;
  mux::ui::looks_shown looks;
  auto chip = mux::ui::reaction_chip(colours, looks, "$message", "a reaction :party:", 2, false,
      {{.shortcode = "party", .url = "mxc://example/party"}});
  EXPECT_EQ(chip.onPress().id, "$message");
  EXPECT_EQ(chip.onPress().key, "a reaction :party:");
}

TEST(Controls, RoomEventChoicesDispatchTypedModelEdits) {
  mux::config::chat_choices initial;
  initial.typing = false;
  skiff::model::Model<mux::config::chat_choices, skiff::bind::NoReactions> model(initial);
  mux::ui::palette colours;
  auto field = mux::ui::event_kinds_field<mux::config::chat_choices>(colours, mux::ui::choice_level::chat{});
  skiff::bind::Binding<decltype(model)> binding;
  binding.refresh(field, model);
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{3}));
  EXPECT_EQ(model.root().room_events, false);
  EXPECT_EQ(model.root().typing, false);
  binding.refresh(field, model);
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{4}));
  ASSERT_TRUE(model.root().room_event_kinds.has_value());
  EXPECT_EQ(mux::ui::events_way_of(mux::ui::choice_level::chat{}, mux::ui::events_of(model.root())), mux::ui::kEventsCustom);
  binding.refresh(field, model);
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{5, 0, 1}));
  EXPECT_EQ(mux::logic::choice_of(model.root().room_event_kinds, mux::all_room_events.front()), true);
  EXPECT_EQ(model.root().typing, false);
}
