// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui, driven as the window's user drives it: pressed and typed into.
import std;
import splice;
import skia;
import skiff.paint;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.nodes.flow;
import skiff.scene;
import skiff.bind;
import skiff.model;
import skiff.widgets.motion;
import mux.core;
import mux.config;
import mux.kept_root;
import mux.plugins.ui;
import mux.ui;
import mux.protocols;
import mux.logic.room_events;
import mux.ui.proto;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
// What a picker page is made of, reached as a test reaches it: the Local
// under its preview, a column -- the search, then for the emoji panel what
// is typed as a reaction, then the sections.
auto& emoji_groups_in(auto& panel) { return std::get<2>(panel.parts.page.fParts); }
auto& sticker_packs_in(auto& page) { return std::get<2>(page.parts.page.fParts); }

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
  void toggle_threads() {}
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
  void menu_select() {}
  void toggle_selected(std::string) {}
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
  void menu_reactions() {}
  void close_reactions() {}
  void open_avatar(std::string) {}
  void open_manage() {}
  void close_dialog() {}
  void open_new_chat() {}
  void close_new_chat() {}
  void start_direct(std::string) {}
  void start_group(std::string) {}
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

  mux::ui::ui_needs needs(stub&) {
    return {.colours = &colours, .emoji = &emoji, .looks = &looks, .paint = &paint, .shared = &shared};
  }
};

// Android is built with static walks. A tap must also survive routing
// without a carried model: the application delivers queued presses later.
TEST(Drawer, MenuTapQueuesAnOpenRequestInPortraitAndLandscape) {
  struct sink {
    int opened = 0;
    void take(const mux::ui::request::open_drawer&) { ++opened; }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  for (const float width : {390.0f, 1000.0f}) {
    auto button = mux::ui::menu_button(colours);
    scene::Scene<decltype(button)> window{std::move(button)};
    window.layoutIfNeeded(skia::SkRect::MakeWH(width, 720.0f));
    const auto bounds = window.root().bounds();
    EXPECT_FLOAT_EQ(bounds.width(), 36.0f);
    EXPECT_FLOAT_EQ(bounds.height(), 36.0f);
    EXPECT_EQ(window.root().semantics().fLabel, "Menu");
    scene::hostWork().pressed.clear();
    scene::hostWork().answers.clear();
    scene::InputRouter router;
    const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
    router.setLayers(layers);
    // The Android long-press handler delivers a short tap as down then up.
    router.pointer(scene::PointerEvent{scene::pointer::down{bounds.centerX(), bounds.centerY()}});
    router.pointer(scene::PointerEvent{scene::pointer::up{bounds.centerX(), bounds.centerY()}});
    const auto presses = std::exchange(scene::hostWork().pressed, {});
    ASSERT_EQ(presses.size(), 1u);
    EXPECT_TRUE(scene::hostWork().answers.empty());
    for (const auto& path : presses)
      ASSERT_TRUE(skiff::bind::press(window.root(), model, path, &requests));
  }
  EXPECT_EQ(requests.opened, 2);
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

TEST(Stickers, PackTabsStayWithinTheFooterAndAllRemainReachable) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct restore_font { ~restore_font() { skiff::paint::defaultFont() = nullptr; } } reset;
  mux::ui::palette colours;
  mux::ui::emoji_kept kept;
  for (int i = 0; i < 32; ++i)
    kept.chat_stickers.push_back({.shortcode = "s" + std::to_string(i), .url = "mxc://example.com/s" + std::to_string(i),
                                  .pack = "Pack " + std::to_string(i)});
  for (const float width : {180.0f, 320.0f}) {
    scene::Scene<mux::plugins::stickers::page_t> window{mux::plugins::stickers::page(colours, kept)};
    auto& footer = sticker_packs_in(window.root()).fStrip.fView;
    const auto frame = [&](double clock) {
      window.update(clock);
      window.layoutIfNeeded(skia::SkRect::MakeWH(width, 400.0f));
      (void)window.finishFrame();
    };
    for (int i = 0; i < 4; ++i)
      frame(1000.0 + i * 16.0);
    auto& tabs = std::get<0>(footer.fChildren).fParts;
    ASSERT_EQ(tabs.size(), 32u);
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
  void take(const mux::ui::request::select_emoji_page& event) {
    asked = event.page == mux::ui::popup_page_t(mux::plugins::gifs::page{});
  }
  template <class Event> void take(const Event&) { ADD_FAILURE() << "Unexpected request from the GIF tab"; }
};
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

}  // namespace

TEST(Calls, PlacementFollowsTheModel) {
  namespace calls = mux::plugins::calls;
  calls::call_shown shown{calls::call_view{}};
  shown.now->phase = calls::call_phase::ringing_in{};
  shown.now->in_view = true;
  EXPECT_TRUE(calls::call_card_of{}(shown).has_value());
  EXPECT_FALSE(calls::call_panel_of{}(shown).has_value());
  shown.now->phase = calls::call_phase::connected{61};
  EXPECT_FALSE(calls::call_card_of{}(shown).has_value());
  EXPECT_TRUE(calls::call_panel_of{}(shown).has_value());
  shown.now->whole = true;
  EXPECT_FALSE(calls::call_card_of{}(shown).has_value());
  EXPECT_FALSE(calls::call_panel_of{}(shown).has_value());
  EXPECT_TRUE(calls::call_screen_of{}(shown).has_value());
  shown.now.reset();
  EXPECT_FALSE(calls::call_screen_of{}(shown).has_value());
}

TEST(Calls, TimerUpdatesTextWithoutRebuildingControls) {
  namespace calls = mux::plugins::calls;
  stub program;
  ui_state ui;
  calls::call_shown started{calls::call_view{}};
  started.now->phase = calls::call_phase::connected{61};
  started.now->encrypted = true;
  auto shown = calls::call_layer(ui.needs(program), calls::call_card_of{}, calls::call_ui::card{}, {.fill = true});
  mux::ui::shown_model model(mux::ui::shown_root{});
  mux::ui::show(model, started);
  skiff::bind::Binding<mux::ui::shown_model> binding;
  binding.refresh(shown, model);
  ASSERT_NE(shown.shown(), nullptr);
  const auto id = shown.shown()->fState.id();
  EXPECT_EQ(std::get<1>(std::get<1>(shown.shown()->fParts).fParts).text(), "1:01");
  auto next = *model.look<calls::call_shown>();
  next.now->phase = calls::call_phase::connected{62};
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
  mux::plugins::proxies::draft draft;
  EXPECT_FALSE(mux::plugins::proxies::profile_of(draft));
  draft.name = "Home";
  draft.host = "localhost";
  for (const auto& port : {"", "0", "65536", "1080junk", "-1"}) {
    draft.port = port;
    EXPECT_FALSE(mux::plugins::proxies::profile_of(draft));
  }
  draft.port = "65535";
  draft.password = "secret";
  const auto profile = mux::plugins::proxies::profile_of(draft);
  ASSERT_TRUE(profile);
  EXPECT_EQ(profile->port, 65535);
  EXPECT_EQ(profile->password, "secret");
  EXPECT_FALSE(profile->username.has_value());
}

TEST(Proxies, RequestsCarryTheDraftAndProfileIndex) {
  const mux::plugins::proxies::draft draft{.index = 2, .name = "Work", .host = "localhost", .port = "1080"};
  const mux::plugins::proxies::draft_events events;
  const auto saved = events.on(mux::plugins::proxies::save_draft{}, draft).fEvent;
  ASSERT_TRUE(saved.profile);
  EXPECT_EQ(saved.index, 2);
  EXPECT_EQ(saved.profile->name, "Work");
  EXPECT_EQ(events.on(mux::plugins::proxies::delete_draft{}, draft).fEvent.index, 2);
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
  // Refresh after press, matching app::after_event(), so LocalPass projects the Local feedback.
  binding.refresh(row, model);
  ASSERT_EQ(requests.copied.size(), 1u);
  EXPECT_EQ(requests.copied.front(), "https://matrix.to/#/#room:example.com");
  EXPECT_EQ(label.text(), "ID · link copied, with its servers");
  EXPECT_EQ(label.fState.id(), id);
  EXPECT_EQ(model.root(), 0);
}

TEST(Controls, TappingTheIdShowsCopyConfirmationInNarrowRows) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
  if (!face) GTEST_SKIP() << "Needs a font for wrapping";
  skiff::paint::fonts().setPrimary(face);
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  struct clear_font { ~clear_font() { skiff::paint::defaultFont() = nullptr; } } clear;
  struct sink {
    std::vector<std::string> copied;
    void take(const mux::ui::request::copy_text& request) { copied.push_back(request.text); }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  mux::ui::palette colours;
  const std::string id = "@someone-with-a-long-name:example.com";
  for (const std::string link : {std::string(), std::string("https://matrix.to/#/") + id}) {
    model_t model(0);
    scene::Scene<mux::ui::id_line_t> view{mux::ui::id_line(colours, id, link)};
    skiff::bind::Binding<model_t> binding;
    binding.refresh(view.root(), model);
    const auto viewport = skia::SkRect::MakeWH(240.0f, 640.0f);
    view.update(0.0);
    view.layoutIfNeeded(viewport);
    (void)view.finishFrame();
    view.update(16.0);
    view.layoutIfNeeded(viewport);
    EXPECT_TRUE(view.finishFrame().fDamage.isEmpty());
    scene::InputRouter router;
    const std::array layers{scene::InputRouter::Layer{view.handle(), false}};
    router.setLayers(layers);
    auto& text = std::get<0>(view.root().fParts);
    const auto box = text.bounds();
    ASSERT_GT(box.width(), 0.0f);
    ASSERT_GT(box.height(), 0.0f);
    scene::hostWork().pressed.clear();
    router.pointer(scene::PointerEvent{scene::pointer::down{box.centerX(), box.centerY()}});
    router.pointer(scene::PointerEvent{scene::pointer::up{box.centerX(), box.centerY()}});
    const auto presses = std::exchange(scene::hostWork().pressed, {});
    ASSERT_EQ(presses.size(), 1u);
    for (const auto& path : presses) ASSERT_TRUE(skiff::bind::press(view.root(), model, path, &requests));
    // Refresh after press, matching app::after_event(), before the next frame lays out the wrapped confirmation.
    binding.refresh(view.root(), model);
    view.update(32.0);
    EXPECT_TRUE(view.layoutIfNeeded(viewport));
    const auto& feedback = std::get<1>(view.root().fParts);
    EXPECT_EQ(feedback.text(), link.empty() ? "ID · copied" : "ID · link copied, with its servers");
    EXPECT_GT(feedback.bounds().width(), 0.0f);
    EXPECT_GT(feedback.bounds().height(), 0.0f);
    EXPECT_TRUE(view.root().bounds().contains(feedback.bounds()));
    EXPECT_GT(feedback.bounds().height(), link.empty() ? 0.0f : 26.0f);
    const auto frame = view.finishFrame();
    EXPECT_TRUE(std::ranges::any_of(frame.fDamageRects, [&](const auto& piece) {
      return skia::SkRect::Intersects(piece, feedback.bounds());
    }));
    ASSERT_FALSE(requests.copied.empty());
    EXPECT_EQ(requests.copied.back(), link.empty() ? id : link);
  }
}

TEST(Proxies, LocalEditorPressesSendTypedSaveAndDeleteRequests) {
  struct sink {
    std::optional<mux::plugins::proxies::save> saved;
    int removed = -1;
    void take(const mux::plugins::proxies::save& request) { saved = request; }
    void take(const mux::plugins::proxies::remove& request) { removed = request.index; }
    void take(const mux::plugins::proxies::open_list&) {}
    void take(const mux::ui::request::close_settings&) {}
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  const mux::config::proxy_settings profile{.name = "Home", .host = "localhost", .port = 1080};
  auto editor = mux::plugins::proxies::editor_view(colours, profile, 2);
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
  ASSERT_TRUE(skiff::bind::press(box, model, scene::Path{3, 1}, &requests));
  ASSERT_TRUE(requests.saved.has_value());
  EXPECT_EQ(requests.saved->text, "Changed");
  EXPECT_EQ(requests.saved->url, "https://example.com");
}

TEST(Forms, LinkEditorActionsAreVisibleAndReceivePointerPresses) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct clear_font { ~clear_font() { skiff::paint::defaultFont() = nullptr; } } clear;
  struct sink {
    int applied = 0, cancelled = 0;
    void take(const mux::ui::request::set_link&) { ++applied; }
    void take(const mux::ui::request::close_link&) { ++cancelled; }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  for (const float width : {240.0f, 390.0f, 1000.0f}) {
    auto box = mux::ui::link_box(colours, {"Text", "https://example.com"});
    scene::Scene<decltype(box)> window{std::move(box)};
    window.layoutIfNeeded(skia::SkRect::MakeWH(width, 400.0f));
    auto& footer = std::get<3>(window.root().fParts);
    const auto cancel = std::get<0>(footer.fParts).bounds();
    const auto apply = std::get<1>(footer.fParts).bounds();
    EXPECT_LT(cancel.fRight, apply.fLeft);
    scene::InputRouter router;
    const std::array layers{scene::InputRouter::Layer{window.handle(), false}};
    router.setLayers(layers);
    for (const auto bounds : {cancel, apply}) {
      ASSERT_GT(bounds.width(), 0.0f);
      ASSERT_GE(bounds.height(), 36.0f);
      EXPECT_GE(bounds.fLeft, window.root().bounds().fLeft);
      EXPECT_LE(bounds.fRight, window.root().bounds().fRight);
      EXPECT_LE(bounds.fBottom, window.root().bounds().fBottom);
      scene::hostWork().pressed.clear();
      router.pointer(scene::PointerEvent{scene::pointer::down{bounds.centerX(), bounds.centerY()}});
      router.pointer(scene::PointerEvent{scene::pointer::up{bounds.centerX(), bounds.centerY()}});
      const auto presses = std::exchange(scene::hostWork().pressed, {});
      ASSERT_EQ(presses.size(), 1u);
      for (const auto& path : presses)
        ASSERT_TRUE(skiff::bind::press(window.root(), model, path, &requests));
    }
  }
  EXPECT_EQ(requests.applied, 3);
  EXPECT_EQ(requests.cancelled, 3);
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


TEST(Selection, TelegramActionsHaveWidthAndDispatchOnDesktopAndPhone) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  for (const float width : {390.0f, 1000.0f}) {
    stub program;
    ui_state ui;
    auto bar = mux::ui::selection_bar(ui.needs(program));
    scene::Scene<decltype(bar)> window{std::move(bar)};
    ui.show(window.root(), mux::ui::selection_shown{2, 1u, true});
    window.layoutIfNeeded(skia::SkRect::MakeWH(width, 60.0f));
    // The plugins' actions, in a row of their own: Forward the first.
    auto& forward = std::get<0>(std::get<0>(window.root().fParts).fParts);
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
      void take(const mux::ui::request::selection_asked<mux::plugins::forward::feature>&) { ++forwarded; }
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
    ui.show(window.root(), mux::ui::selection_shown{3, 1u, false});
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

TEST(Images, LateAnimationsWakeSettledImagesWithoutScrolling) {
  auto still = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  auto moving = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  ASSERT_TRUE(still);
  ASSERT_TRUE(moving);
  const std::string key = "late-animation";
  const auto check = [&](auto source, auto& images, auto& frames) {
    frames.clear();
    images.put(key, still->makeImageSnapshot());
    auto root = skiff::compose::column(skiff::compose::vbox(0.0f, {.fill = true}),
        skiff::compose::styled({.fill = true}, skiff::nodes::Image<decltype(source)>(source)));
    scene::Scene<decltype(root)> view(std::move(root));
    const auto viewport = skia::SkRect::MakeWH(8.0f, 8.0f);
    view.update(0.0);
    view.layoutIfNeeded(viewport);
    (void)view.finishFrame();
    view.update(16.0);
    view.layoutIfNeeded(viewport);
    const auto settled = view.finishFrame();
    EXPECT_TRUE(settled.fDamage.isEmpty());
    EXPECT_FALSE(std::isfinite(settled.fWakeAtMs));
    auto& image = std::get<0>(view.root().fParts);
    EXPECT_FALSE(image.wantsTick());
    EXPECT_FALSE(scene::work::ticking().contains(image.fState.id()));

    frames.put(key, {{moving->makeImageSnapshot(), 40}, {still->makeImageSnapshot(), 100}});
    EXPECT_TRUE(scene::work::pending().contains(image.fState.id()));
    view.update(32.0);
    view.layoutIfNeeded(viewport);
    const auto animated = view.finishFrame();
    EXPECT_TRUE(scene::work::ticking().contains(image.fState.id()));
    EXPECT_TRUE(std::isfinite(animated.fWakeAtMs));
    EXPECT_FALSE(animated.fWantsAnotherFrame);

    frames.clear();
    view.update(48.0);
    view.layoutIfNeeded(viewport);
    EXPECT_FALSE(std::isfinite(view.finishFrame().fWakeAtMs));
    EXPECT_FALSE(image.wantsTick());
  };
  check(mux::ui::from_moving_thumbnail{key}, mux::ui::thumbnails(), mux::ui::animations());
  check(mux::ui::from_moving_whole{key}, mux::ui::whole_pictures(), mux::ui::animations());
  check(mux::ui::from_emotes{key}, mux::ui::emoji_images(), mux::ui::emoji_animations());
  check(mux::ui::from_moving_thumbnail{key, true}, mux::ui::sticker_images(), mux::ui::sticker_animations());
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

TEST(Reactions, MixedCustomEmojiKeepTheOriginalReactionKeyWhenClicked) {
  mux::ui::palette colours;
  mux::ui::looks_shown looks;
  auto chip = mux::plugins::reactions::reaction_chip(colours, looks, "$message", "a reaction :party:", 2, false,
      {{.shortcode = "party", .url = "mxc://example/party"}});
  EXPECT_EQ(chip.onPress().id, "$message");
  EXPECT_EQ(chip.onPress().key, "a reaction :party:");
}

TEST(Controls, RoomEventsDropdownKeepsDisabledShowHideRows) {
  mux::config::chat_choices initial;
  initial.typing = false;
  skiff::model::Model<mux::config::chat_choices, skiff::bind::NoReactions> model(initial);
  mux::ui::palette colours;
  auto field = mux::ui::event_kinds_field<mux::config::chat_choices>(colours, mux::choice_level::chat{});
  auto& mode = std::get<0>(field.fParts);
  auto& rows = std::get<1>(field.fParts);
  skiff::bind::Binding<decltype(model)> binding;
  binding.refresh(field, model);
  EXPECT_EQ(mode.names.front(), "As above");
  EXPECT_EQ(mode.current, 0u);
  EXPECT_TRUE(rows.visible());
  EXPECT_TRUE(rows.fParts.front().disabled());
  mode.picked = 2;
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{0}));
  binding.refresh(field, model);
  EXPECT_EQ(model.root().room_events, false);
  EXPECT_EQ(mode.current, 2u);
  EXPECT_TRUE(rows.fParts.front().disabled());
  mode.picked = 3;
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{0}));
  binding.refresh(field, model);
  ASSERT_TRUE(model.root().room_event_kinds.has_value());
  EXPECT_EQ(mode.current, 3u);
  auto& kind = rows.fParts.front();
  EXPECT_EQ(std::tuple_size_v<std::remove_reference_t<decltype(std::get<1>(kind.fParts).fParts)>>, 1u);
  EXPECT_EQ(std::get<0>(std::get<1>(kind.fParts).fParts).text(), "Show");
  EXPECT_EQ(std::get<0>(std::get<2>(kind.fParts).fParts).text(), "Hide");
  EXPECT_FALSE(kind.disabled());
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{1, 0, 1}));
  binding.refresh(field, model);
  EXPECT_EQ(mux::logic::choice_of(model.root().room_event_kinds, mux::all_room_events.front()), true);
  EXPECT_TRUE(std::get<1>(kind.fParts).fState.fSelected);
  EXPECT_EQ(model.root().typing, false);
  mode.picked = 1;
  ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{0}));
  binding.refresh(field, model);
  EXPECT_EQ(mode.current, 1u);
  EXPECT_TRUE(rows.visible());
  EXPECT_TRUE(kind.disabled());
}

TEST(Controls, RoomEventsAllSurvivesReopeningAndOpeningItsHeader) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct cleanup { ~cleanup() { skiff::paint::defaultFont() = nullptr; } } clear;
  using choices = mux::config::chat_choices;
  const mux::account_id account{mux::protocol::matrix{}, "@alice:example.org"};
  const mux::conversation_id room{account, "!room:example.org"};
  struct root { skiff::model::Keyed<mux::conversation_id, choices> chats; } initial;
  initial.chats.put(room, choices{});
  skiff::model::Model<root, skiff::bind::NoReactions> model(std::move(initial));
  mux::ui::palette colours;
  const auto exercise = [&](bool select_all) {
    auto page = mux::ui::chat_settings_view(colours, room);
    scene::Scene<decltype(page)> view{std::move(page)};
    skiff::bind::Binding<decltype(model)> binding;
    binding.refresh(view.root(), model);
    const auto viewport = skia::SkRect::MakeWH(400.0f, 1100.0f);
    view.layoutIfNeeded(viewport);
    auto& field = std::get<0>(view.root().fParts);
    auto& mode = std::get<0>(field.fParts);
    EXPECT_EQ(mode.current, select_all ? 0u : 1u);
    const auto press = [&](const skia::SkRect& bounds) {
      scene::hostWork().pressed.clear();
      ASSERT_TRUE(view.dispatchPointer(scene::pointer::down{bounds.centerX(), bounds.centerY()}));
      for (const auto& path : std::exchange(scene::hostWork().pressed, {}))
        ASSERT_TRUE(skiff::bind::press(view.root(), model, path));
      ASSERT_TRUE(view.dispatchPointer(scene::pointer::up{bounds.centerX(), bounds.centerY()}));
      binding.refresh(view.root(), model);
      view.layoutIfNeeded(viewport);
    };
    press(mode.parts.head.bounds());
    EXPECT_TRUE(mode.open);
    if (select_all) press(mode.parts.options[1].bounds());
    ASSERT_NE(model.root().chats.find(room), nullptr);
    EXPECT_EQ(model.root().chats.find(room)->room_events, true);
  };
  exercise(true);
  exercise(false);
}

// No UI choice may be delivered as an application request. This sink makes
// the test fail to compile if the wrong model attempts to dispatch one.
namespace {
struct room_events_no_requests {
  template <class E> void take(const E&) = delete;
};
struct room_events_other_root { int unrelated = 0; };
}
TEST(Controls, RoomEventsDropdownDefersToItsOwningModel) {
  mux::ui::palette colours;
  auto field = mux::ui::event_kinds_field<mux::config::chat_choices>(colours, mux::choice_level::chat{});
  skiff::model::Model<room_events_other_root, skiff::bind::NoReactions> other;
  skiff::model::Model<mux::config::chat_choices, skiff::bind::NoReactions> owner;
  room_events_no_requests sink;
  std::get<0>(field.fParts).picked = 3;
  EXPECT_FALSE(skiff::bind::press(field, other, scene::Path{0}, &sink));
  ASSERT_TRUE(skiff::bind::press(field, owner, scene::Path{0}, &sink));
  auto& kind = std::get<1>(field.fParts).fParts.front();
  EXPECT_FALSE(skiff::bind::press(field, other, scene::Path{1, 0, 2}, &sink));
  ASSERT_TRUE(skiff::bind::press(field, owner, scene::Path{1, 0, 2}, &sink));
  EXPECT_EQ(mux::logic::choice_of(owner.root().room_event_kinds, mux::all_room_events.front()), false);
  auto& show = std::get<1>(kind.fParts);
  EXPECT_FALSE(skiff::bind::answerPress(show, skiff::bind::carryFrom(&other, &sink)));
  EXPECT_TRUE(skiff::bind::answerPress(show, skiff::bind::carryFrom(&owner, &sink)));
  EXPECT_EQ(mux::logic::choice_of(owner.root().room_event_kinds, mux::all_room_events.front()), true);
}

TEST(Reactions, Msc4027ImagesAndTextRenderTogetherInPanelBodies) {
  const auto body = mux::ui::reaction_body_of("before mxc://example/one mxc://example/two mxc://example/one after", {});
  ASSERT_TRUE(body.html.has_value());
  const auto read = mux::ui::read_html(*body.html);
  ASSERT_EQ(read.spans.size(), 3u);
  EXPECT_TRUE(read.text.starts_with("before "));
  EXPECT_TRUE(read.text.ends_with(" after"));
  EXPECT_EQ(read.spans[0].target, "mxc://example/one");
  EXPECT_EQ(read.spans[1].target, "mxc://example/two");
  EXPECT_EQ(read.spans[2].target, "mxc://example/one");
}
TEST(History, ReactionsTargetEachEditAndNeverGuessMissingEditIds) {
  mux::message message{.id = "$original", .body = {"third", std::nullopt}, .edited = true};
  message.versions = {{{"first", std::nullopt}, {}, "$original"}, {{"second", std::nullopt}, {}, "$edit1"}};
  message.latest_edit_event = "$edit2";
  const auto versions = mux::plugins::edit_history::edit_versions_of(message);
  ASSERT_EQ(versions.size(), 3u);
  EXPECT_EQ(versions[0].id, "$original");
  EXPECT_EQ(versions[1].id, "$edit1");
  EXPECT_EQ(versions[2].id, "$edit2");
  message.versions[1].event.clear();
  message.latest_edit_event.clear();
  const auto legacy = mux::plugins::edit_history::edit_versions_of(message);
  EXPECT_EQ(legacy[0].id, "$original");
  EXPECT_TRUE(legacy[1].id.empty());
  EXPECT_TRUE(legacy[2].id.empty());
}

TEST(Explore, RepliesBelongToTheOpeningAccountAndSearch) {
  const mux::account_id xmpp{mux::protocol::xmpp{}, "alice@example.org"};
  const mux::account_id matrix{mux::protocol::matrix{}, "@alice:example.org"};
  mux::plugins::explore::shown facts{.by = xmpp, .server = "rooms.example.org", .query = "lounge"};
  mux::change::directory_listed reply{.by = matrix, .server = facts.server, .query = facts.query};
  EXPECT_FALSE(mux::plugins::explore::accepts_listing(facts, reply));
  reply.by = xmpp;
  EXPECT_TRUE(mux::plugins::explore::accepts_listing(facts, reply));
  reply.query = "old query";
  EXPECT_FALSE(mux::plugins::explore::accepts_listing(facts, reply));
  reply.query = facts.query;
  reply.server = "other.example.org";
  EXPECT_FALSE(mux::plugins::explore::accepts_listing(facts, reply));
  reply.server = facts.server;
  reply.space = "!space:example.org";
  EXPECT_FALSE(mux::plugins::explore::accepts_listing(facts, reply));
}

TEST(Protocols, XmppFormsAndEventControlsUseItsCapabilities) {
  const auto xmpp = mux::state_before(mux::protocol::xmpp{});
  const auto matrix = mux::state_before(mux::protocol::matrix{});
  EXPECT_EQ(mux::proto::directory_server_of(xmpp, "alice@example.org/client"), "example.org");
  EXPECT_TRUE(mux::proto::offers(xmpp, mux::proto::feature::room_directory{}));
  EXPECT_TRUE(mux::proto::offers(xmpp, mux::proto::feature::room_creation{}));
  EXPECT_FALSE(mux::proto::offers(xmpp, mux::proto::feature::sticker_packs{}));
  EXPECT_TRUE(mux::proto::room_event_kinds_of(xmpp).empty());
  EXPECT_EQ(mux::proto::room_event_kinds_of(matrix).size(), mux::all_room_events.size());
  const auto form = mux::proto::room_creation_form_of(xmpp, "example.org");
  EXPECT_TRUE(form.address_required);
  EXPECT_FALSE(form.encryption);
  EXPECT_FALSE(form.federation);
  mux::ui::new_room_facts facts{.own_server = "example.org", .by = mux::account_id{mux::protocol::xmpp{}, "alice@example.org"}};
  mux::ui::room_creation_events submit{facts, form};
  mux::ui::room_draft draft{.name = "Lounge", .topic = "Welcome", .address = "lounge@rooms.example.org"};
  const auto event = submit.on(mux::ui::submit_room{}, draft).fEvent;
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->alias, draft.address);
  EXPECT_EQ(event->name, draft.name);
  EXPECT_FALSE(event->encrypted);
  draft.address.clear();
  EXPECT_FALSE(submit.on(mux::ui::submit_room{}, draft).fEvent.has_value());
}

TEST(StartChat, XmppAddressesAndKnownContactsStayInItsForm) {
  mux::ui::new_chat_facts facts{.people = {{"bob@example.org", "Bob"}},
      .by = mux::account_id{mux::protocol::xmpp{}, "alice@example.org"}};
  EXPECT_FALSE(mux::ui::direct_address(facts, "@bob:example.org"));
  EXPECT_TRUE(mux::ui::direct_address(facts, "new@example.org"));
  facts.query = "new@example.org";
  const auto rows = mux::ui::people_rows(facts);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ((*rows.begin()).first, facts.query);
  facts.query = "bob";
  const auto known = mux::ui::people_rows(facts);
  ASSERT_EQ(known.size(), 1u);
  EXPECT_EQ((*known.begin()).first, "bob@example.org");
}

TEST(RoomInvites, PickerRequestsKeepTheRoomAccountForBothProtocols) {
  const mux::account_id matrix{mux::protocol::matrix{}, "@alice:example.org"};
  const mux::account_id xmpp{mux::protocol::xmpp{}, "alice@example.org"};
  for (const auto& [account, room_id, user] : std::array{
      std::tuple{matrix, std::string("!room:example.org"), std::string("@bob:example.org")},
      std::tuple{xmpp, std::string("room@rooms.example.org"), std::string("bob@example.org")}}) {
    mux::ui::new_chat_facts facts{.query = user, .by = account,
        .inviting = mux::conversation_id{account, room_id}, .room_name = "Lounge"};
    const auto event = mux::ui::start_chat_request(facts);
    ASSERT_TRUE(event.has_value());
    const auto& invite = spl::get<mux::ui::request::invite_to_room>(*event);
    EXPECT_EQ(invite.room, *facts.inviting);
    EXPECT_EQ(invite.user, user);
    facts.inviting.reset();
    const auto direct = mux::ui::start_chat_request(facts);
    ASSERT_TRUE(direct.has_value());
    EXPECT_EQ(spl::get<mux::ui::request::start_direct>(*direct).user, user);
  }
}

TEST(Controls, ScalarSettingsIgnoreOtherModelsAndChangeOnlyTheirNamedField) {
  using choices = mux::config::chat_choices;
  using field = skiff::model::Field<&choices::receipts>;
  mux::ui::palette colours;
  auto view = mux::ui::show_hide_field<mux::ui::receipts_setting, std::optional<bool>, field>(
      colours, mux::choice_level::chat{});
  // The window model contains many bools and optional bools. This control
  // belongs to the settings model, even when the window binding walks it.
  mux::ui::shown_model shown(mux::ui::shown_root{});
  skiff::bind::Binding<decltype(shown)> window_binding;
  window_binding.refresh(view, shown);
  choices initial;
  initial.receipts = false;
  initial.typing = true;
  skiff::model::Model<choices, skiff::bind::NoReactions> model(initial);
  skiff::bind::Binding<decltype(model)> binding;
  binding.refresh(view, model);
  EXPECT_TRUE(std::get<3>(view.fParts).fState.fSelected);
  ASSERT_TRUE(skiff::bind::press(view, model, scene::Path{2}));
  binding.refresh(view, model);
  EXPECT_EQ(model.root().receipts, true);
  EXPECT_EQ(model.root().typing, true);
  EXPECT_TRUE(std::get<2>(view.fParts).fState.fSelected);
  auto limit = mux::ui::jump_search_field<std::optional<std::int64_t>, skiff::model::Field<&choices::jump_search>>(
      colours, mux::choice_level::chat{});
  window_binding.refresh(limit, shown);
  binding.refresh(limit, model);
  ASSERT_TRUE(skiff::bind::press(limit, model, scene::Path{2, 0}));
  EXPECT_EQ(model.root().jump_search, 500);
  EXPECT_EQ(model.root().receipts, true);
}

}

TEST(StartChat, BoundGoEmitsRealRequestsAndRowsIgnoreOtherModels) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct cleanup { ~cleanup() { skiff::paint::defaultFont() = nullptr; } } clear;
  mux::ui::palette colours;
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.org"};
  mux::ui::new_chat_facts facts{.query = "@bob:example.org", .by = alice};
  facts.rows = mux::ui::people_rows(facts);
  auto view = mux::ui::start_chat_view(colours, facts);
  skiff::model::Model<mux::ui::new_chat_facts, skiff::bind::NoReactions> owner(facts);
  mux::model other;
  skiff::bind::refresh(view, other.chats());
  skiff::bind::refresh(view, owner);
  skiff::bind::refresh(view, other.chats());
  auto& go = std::get<1>(std::get<2>(view.fParts).fParts);
  auto asked = go.onPress();
  ASSERT_TRUE(asked.has_value());
  EXPECT_TRUE(spl::holds_alternative<mux::ui::request::start_direct>(*asked));
  EXPECT_EQ(spl::get<mux::ui::request::start_direct>(*asked).user, "@bob:example.org");
  facts.inviting = mux::conversation_id{alice, "!room:example.org"};
  go.read(facts);
  asked = go.onPress();
  ASSERT_TRUE(asked.has_value());
  EXPECT_TRUE(spl::holds_alternative<mux::ui::request::invite_to_room>(*asked));
  EXPECT_EQ(spl::get<mux::ui::request::invite_to_room>(*asked).room, *facts.inviting);
}

TEST(Controls, MemoizedRoomIdKeepsCopyFeedbackWithoutAnOuterModelChange) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct clear_font { ~clear_font() { skiff::paint::defaultFont() = nullptr; } } clear;
  struct sink {
    std::vector<std::string> copied;
    void take(const mux::ui::request::copy_text& request) { copied.push_back(request.text); }
  } requests;
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t model(0);
  mux::ui::palette colours;
  scene::Scene<skiff::nodes::Memo<int, mux::ui::id_line_t>> view;
  auto& header = view.root();
  header.fState.apply({.fillX = true, .autoSize = scene::axes::kY});
  header.show(1, [&](int) { return mux::ui::id_line(colours, "!room:example.com", "https://matrix.to/#/!room:example.com"); });
  skiff::bind::Binding<model_t> binding;
  binding.refresh(header, model);
  const auto viewport = skia::SkRect::MakeWH(240.0f, 640.0f);
  view.update(0.0);
  view.layoutIfNeeded(viewport);
  (void)view.finishFrame();
  view.update(16.0);
  view.layoutIfNeeded(viewport);
  EXPECT_TRUE(view.finishFrame().fDamage.isEmpty());
  auto& label = std::get<1>(header.content()->fParts);
  const auto id = label.fState.id();
  ASSERT_TRUE(skiff::bind::press(header, model, scene::Path{0}, &requests));
  // Refresh after press, matching app::after_event(), so LocalPass enters Memo and projects the Local feedback.
  binding.refresh(header, model);
  ASSERT_EQ(requests.copied.size(), 1u);
  EXPECT_EQ(label.text(), "ID · link copied, with its servers");
  EXPECT_EQ(label.fState.id(), id);
  EXPECT_EQ(model.root(), 0);
  view.update(32.0);
  EXPECT_TRUE(view.layoutIfNeeded(viewport));
  EXPECT_TRUE(header.bounds().contains(label.bounds()));
  const auto frame = view.finishFrame();
  EXPECT_TRUE(std::ranges::any_of(frame.fDamageRects, [&](const auto& piece) {
    return skia::SkRect::Intersects(piece, label.bounds());
  }));
}

TEST(Composer, ReplacementActionRemainsVisibleInNarrowPanes) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
  if (!face) GTEST_SKIP() << "Needs a font for wrapping";
  skiff::paint::fonts().setPrimary(face);
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  struct clear_font { ~clear_font() { skiff::paint::defaultFont() = nullptr; } } clear;
  mux::ui::palette colours;
  using bar_t = decltype(mux::ui::replaced_room_bar(colours));
  scene::Scene<bar_t> view{std::in_place, mux::ui::replaced_room_bar(colours)};
  for (float width : {240.0f, 340.0f, 720.0f}) {
    view.layoutIfNeeded(skia::SkRect::MakeWH(width, 400.0f));
    const auto& bar = view.root();
    const auto& button = std::get<1>(bar.fParts);
    EXPECT_EQ(button.semantics().fLabel, "Join New Room");
    EXPECT_TRUE(button.visible());
    EXPECT_GE(button.bounds().fLeft, bar.bounds().fLeft);
    EXPECT_LE(button.bounds().fRight, bar.bounds().fRight);
    EXPECT_GT(button.bounds().height(), 0.0f);
    EXPECT_GE(button.bounds().fTop, std::get<0>(bar.fParts).bounds().fBottom);
  }
}

TEST(Controls, ChatNotificationChoicesApplyValuesAndPreserveOtherSettings) {
  mux::config::chat_choices initial;
  initial.muted = true;
  initial.notify.on = true;
  initial.notify.mentions = false;
  initial.notify.sound = true;
  initial.typing = false;
  initial.previews = false;
  using model_t = skiff::model::Model<mux::config::chat_choices, skiff::bind::NoReactions>;
  model_t model(initial);
  mux::ui::palette colours;
  auto field = mux::ui::chat_on_field(colours);
  skiff::bind::Binding<model_t> binding;
  binding.refresh(field, model);
  const auto id = std::get<2>(field.fParts).fState.id();
  for (const auto& [button, on] : std::array<std::pair<std::uint32_t, std::optional<bool>>, 4>{
           std::pair{2u, std::optional<bool>(true)}, {3u, false}, {1u, std::nullopt}, {2u, true}}) {
    ASSERT_TRUE(skiff::bind::press(field, model, scene::Path{button}));
    binding.refresh(field, model);
    auto expected = initial;
    expected.muted = on == false;
    expected.notify.on = on == true ? on : std::nullopt;
    EXPECT_EQ(model.root(), expected);
    EXPECT_EQ(std::get<2>(field.fParts).fState.id(), id);
  }
}

TEST(RoomCreation, DialogRefreshesLocalChoicesWithoutAnotherModelChange) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  struct cleanup { ~cleanup() { skiff::paint::defaultFont() = nullptr; } } clear;
  struct sink {
    void take(const mux::ui::request::create_room&) {}
    void take(const mux::ui::request::close_new_room&) {}
  } requests;
  mux::ui::palette colours;
  const mux::ui::new_room_facts facts{.own_server = "example.org",
      .by = mux::account_id{mux::protocol::matrix{}, "@alice:example.org"}};
  skiff::widgets::Dialog<mux::ui::create_room_box_t> dialog;
  auto& view = dialog.open(mux::ui::create_room_view(colours, facts));
  mux::ui::shown_model owner(mux::ui::shown_root{});
  skiff::bind::Binding<mux::ui::shown_model> binding;
  binding.refresh(dialog, owner);
  auto& form = std::get<0>(std::get<1>(view.fParts).fChildren);
  const auto revision = owner.revision();
  // Dialog children: scrim, sheet, content. Form: scroll, content, Anyone.
  ASSERT_TRUE(skiff::bind::press(dialog, owner, scene::Path{2, 1, 0, 4}, &requests));
  binding.refresh(dialog, owner);
  EXPECT_EQ(owner.revision(), revision);
  EXPECT_EQ(view.fModel.root().access, mux::ui::room_access_t(mux::ui::room_access::everyone{}));
  EXPECT_TRUE(std::get<4>(form.fParts).fState.selected());
  EXPECT_TRUE(std::get<6>(form.fParts).visible());
  EXPECT_FALSE(std::get<7>(form.fParts).visible());
  ASSERT_TRUE(skiff::bind::press(dialog, owner, scene::Path{2, 1, 0, 3}, &requests));
  binding.refresh(dialog, owner);
  EXPECT_TRUE(std::get<7>(form.fParts).visible());
  // Encryption toggle is the second child of its row.
  ASSERT_TRUE(skiff::bind::press(dialog, owner, scene::Path{2, 1, 0, 7, 1}, &requests));
  binding.refresh(dialog, owner);
  EXPECT_FALSE(view.fModel.root().encrypted);
  const auto& footer = std::get<2>(view.fParts);
  EXPECT_GT(std::get<0>(footer.fParts).fState.fWidth, 0.0f);
  EXPECT_GT(std::get<1>(footer.fParts).fState.fWidth, 0.0f);
}

TEST(Search, RoomAddressSuggestionsRespectSelectedProtocol) {
  const mux::account_id xmpp{mux::protocol::xmpp{}, "alice@example.org"};
  const mux::account_id matrix{mux::protocol::matrix{}, "@alice:example.org"};
  EXPECT_EQ(mux::ui::room_address_for(xmpp, "lounge@conference.example.org"),
            std::optional<std::string>("lounge@conference.example.org"));
  EXPECT_EQ(mux::ui::room_address_for(matrix, "#lounge:example.org"),
            std::optional<std::string>("#lounge:example.org"));
  EXPECT_FALSE(mux::ui::room_address_for(matrix, "lounge@conference.example.org").has_value());
  EXPECT_FALSE(mux::ui::room_address_for(xmpp, "#lounge:example.org").has_value());
  EXPECT_FALSE(mux::ui::room_address_for(matrix, "@alice:example.org").has_value());
}

TEST(Appearance, RoomStylesUseTheirOwnAccountAndNearestSpace) {
  mux::kept_root kept;
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.org"};
  const mux::account_id bob{mux::protocol::matrix{}, "@bob:example.org"};
  const mux::conversation_id room{alice, "!room:example.org"}, space{alice, "!space:example.org"};
  kept.looks.fValue.style = {.font = "Client font", .monospace = "Client code"};
  kept.accounts.put(alice.address, mux::config::account_t{.shared = {.style = {.font = "Alice font"}}});
  kept.accounts.put(bob.address, mux::config::account_t{.shared = {.style = {.font = "Bob font"}}});
  kept.chats.put(space, mux::config::chat_choices{.style = {.theme = mux::config::theme::night{}}});
  kept.chats.put(room, mux::config::chat_choices{.style = {.monospace = "Room code"}});
  std::map<mux::conversation_id, mux::conversation_id> parents{{room, space}};
  const mux::chat_settings reads{&kept, &parents};
  const auto selected = reads.style_of(bob, room);
  EXPECT_EQ(selected.font, "Alice font");
  EXPECT_EQ(selected.monospace, "Room code");
  EXPECT_EQ(selected.theme, std::optional<mux::config::theme_t>(mux::config::theme::night{}));
  EXPECT_EQ(reads.style_of(bob, std::nullopt).font, "Bob font");
  EXPECT_EQ(reads.style_of(std::nullopt, std::nullopt).font, "Client font");
}


TEST(Verification, EmojiWrapWithinPhoneWidthsWithoutChangingOrder) {
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  mux::ui::palette colours;
  const mux::verification_step_t step = mux::verification_step::compare{{0, 1, 2, 3, 4, 5, 6}};
  for (float width : {240.0f, 320.0f, 390.0f}) {
    auto row = mux::ui::emoji_row(colours, step);
    scene::layout(row, skia::SkRect::MakeWH(width, 400.0f));
    const auto& cells = std::get<0>(row.fChildren);
    ASSERT_EQ(cells.size(), 7u);
    for (std::size_t i = 0; i < cells.size(); ++i) {
      const auto box = cells[i].bounds();
      EXPECT_GE(box.fLeft, 0.0f);
      EXPECT_LE(box.fRight, width);
      EXPECT_EQ(std::get<0>(cells[i].fParts).text(), mux::sas_emoji[i].first);
      if (i != 0) {
        const auto previous = cells[i - 1].bounds();
        if (box.fTop == previous.fTop) EXPECT_GE(box.fLeft, previous.fRight);
        else EXPECT_GE(box.fTop, previous.fBottom);
      }
    }
    EXPECT_GT(cells.back().bounds().fTop, cells.front().bounds().fTop);
  }
  skiff::paint::defaultFont() = nullptr;
}

TEST(Info, MatrixProfileIdCopiesItsLinkAndImmediatelyConfirmsIt) {
  ui_state ui;
  const mux::account_id alice{mux::protocol::matrix{}, "@alice:example.org"};
  const std::string bob = "@bob:example.org";
  ui.shared.protocol_states[alice] = mux::proto::matrix::state{.online = true};
  mux::ui::person_facts facts;
  facts.name = "Bob";
  auto details = mux::ui::person_card_details(ui.colours, ui.shared, alice, bob, facts);
  auto& id = std::get<2>(details.fParts);
  const auto copied = id.onPress();
  EXPECT_EQ(copied.text, "https://matrix.to/#/@bob:example.org");
  EXPECT_EQ(id.parts.feedback.text(), "ID · link copied, with its servers");
}


TEST(Appearance, AvatarRoundingIsProportionalAndSeparateForChatsAndPeople) {
  EXPECT_FLOAT_EQ(mux::ui::avatar_radius(40.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(mux::ui::avatar_radius(40.0f, 25.0f), 5.0f);
  EXPECT_FLOAT_EQ(mux::ui::avatar_radius(96.0f, 25.0f), 12.0f);
  EXPECT_FLOAT_EQ(mux::ui::avatar_radius(40.0f, 100.0f), 20.0f);
  const auto before = mux::ui::avatar_rounding();
  mux::ui::use_avatar_rounding(25.0f, 50.0f);
  mux::ui::avatar_mark chat(mux::ui::avatar_of::chat{}, "!room:example.org", "Garden", 40.0f);
  EXPECT_FLOAT_EQ(chat.fState.fCornerRadius, 5.0f);
  EXPECT_FLOAT_EQ(chat.parts.picture.fState.fCornerRadius, 5.0f);
  chat.show("!other:example.org", "Other");
  EXPECT_FLOAT_EQ(chat.fState.fCornerRadius, 5.0f);
  mux::ui::avatar_mark person(mux::ui::avatar_of::person{}, "@ann:example.org", "Ann", 40.0f);
  EXPECT_FLOAT_EQ(person.fState.fCornerRadius, 10.0f);
  EXPECT_FLOAT_EQ(person.parts.picture.fState.fCornerRadius, 10.0f);
  mux::ui::use_avatar_rounding(before.chats, before.people);
}

TEST(Appearance, LocalThemeColourFieldsRefreshWithoutAKeyedList) {
  mux::ui::palette colours;
  auto editor = mux::ui::theme_details_view(colours, mux::config::theme::day{}, {});
  using model_t = skiff::model::Model<int, skiff::bind::NoReactions>;
  model_t outer(0);
  skiff::bind::Binding<model_t> binding;
  binding.refresh(editor, outer);
  auto& details = std::get<1>(editor.fParts);
  auto& fields = std::get<2>(details.fParts);
  static_assert(std::tuple_size_v<decltype(fields.fParts)> == 19);
  EXPECT_TRUE(editor.fModel.root().colours.background.empty());
}

TEST(Appearance, ThemeConfigurationValidatesColorsAndPreservesAlpha) {
  EXPECT_EQ(mux::ui::theme_colour_of("#123456"), 0xff123456u);
  EXPECT_EQ(mux::ui::theme_colour_of("80123456"), 0x80123456u);
  EXPECT_FALSE(mux::ui::theme_colour_of("#12"));
  EXPECT_FALSE(mux::ui::theme_colour_of("#GGGGGG"));
  EXPECT_EQ(mux::ui::theme_colour_word(0x80123456u), "#80123456");
  mux::config::custom_theme custom;
  custom.background = 0xff112233u;
  custom.corner_radius = 14.0;
  const mux::config::theme_t selected = mux::config::theme::day{};
  auto draft = mux::ui::theme_details_initial(selected, {{"day", custom}});
  EXPECT_EQ(draft.colours.background, "#112233");
  EXPECT_EQ(draft.corner, "14");
  const auto saved = mux::ui::theme_details_events{}.on(mux::ui::save_theme_details{}, draft);
  const auto& asked = std::get<1>(saved).fEvent;
  ASSERT_TRUE(asked);
  ASSERT_TRUE(asked->custom);
  EXPECT_EQ(asked->theme, selected);
  EXPECT_EQ(asked->custom->background, custom.background);
  EXPECT_EQ(asked->custom->corner_radius, custom.corner_radius);
  auto colours = mux::ui::palette_of(selected, mux::config::accent::theme_own{}, 100);
  mux::ui::apply_theme(colours, *asked->custom);
  EXPECT_EQ(colours.background, *custom.background);
  EXPECT_EQ(colours.widgets.fCorner, 14.0f);
  draft.font = "100";
  const auto invalid = mux::ui::theme_details_events{}.on(mux::ui::save_theme_details{}, draft);
  EXPECT_FALSE(std::get<1>(invalid).fEvent);
  const auto reset = mux::ui::theme_details_events{}.on(mux::ui::reset_theme_details{}, draft);
  EXPECT_FALSE(reset.fEvent.custom);
}

TEST(RoomManagement, CompactSelectorsLeaveRoomForTheirLabels) {
  auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
  auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
  if (!face) GTEST_SKIP() << "Needs a font for layout";
  skia::SkFont font(face);
  skiff::paint::defaultFont() = &font;
  struct cleanup { ~cleanup() { skiff::paint::defaultFont() = nullptr; } } clear;
  namespace manage = mux::proto::matrix::manage_detail;
  const auto colours = mux::proto::matrix::manage_style(mux::proto::matrix::state{}, mux::ui::palette{}).colours;
  auto row = manage::tile(colours, "Send Messages", "",
      manage::compact_choice(colours, "", {"Member 1 & Above", "Admin & Above"}, 0,
          [](std::size_t) { return skiff::model::Nothing{}; }));
  auto& tile = std::get<0>(row.fParts);
  auto& label = std::get<0>(tile.fParts);
  auto& selector = std::get<1>(tile.fParts);
  for (float width : {330.0f, 556.0f}) {
    scene::layout(row, skia::SkRect::MakeWH(width, 200.0f));
    EXPECT_GT(label.bounds().width(), 0.0f);
    EXPECT_LE(label.bounds().fRight, selector.bounds().fLeft);
    EXPECT_LE(selector.bounds().fRight, width - 12.0f);
    EXPECT_LE(selector.bounds().width(), 180.0f);
  }
}

TEST(Plugins, TypingSaysWhoIsTypingAsTelegramDoes) {
  mux::conversation chat;
  chat.members = {{.id = "@a:x.org", .name = "Ann"}, {.id = "@b:x.org", .name = "Bob"}, {.id = "@c:x.org", .name = "Cy"},
                  {.id = "@d:x.org", .name = "Dee"}};
  const auto said = [&] {
    return extend(mux::contract::chat_status{}, mux::type_tag<mux::plugins::typing::feature>{}, std::as_const(chat));
  };
  EXPECT_FALSE(said());
  chat.typing = {"@a:x.org"};
  EXPECT_EQ(said().value_or(""), "Ann is typing…");
  chat.typing = {"@a:x.org", "@b:x.org"};
  EXPECT_EQ(said().value_or(""), "Ann and Bob are typing…");
  chat.typing = {"@a:x.org", "@b:x.org", "@c:x.org", "@d:x.org"};
  EXPECT_EQ(said().value_or(""), "Ann, Bob and 2 more are typing…");
}

TEST(Plugins, PreviewLineRunsWhitespaceTogetherAndCutsAtAWord) {
  EXPECT_EQ(mux::plugins::previews::preview_line("  one\n\ttwo   three "), "one two three");
  const std::string long_line = std::string(150, 'a') + " " + std::string(100, 'b');
  const std::string cut = mux::plugins::previews::preview_line(long_line);
  EXPECT_EQ(cut, std::string(150, 'a') + "…");
}

TEST(Plugins, PinnedBarShowsWhileThereIsAPin) {
  namespace pins = mux::plugins::pins;
  ui_state ui;
  auto bar = pins::pinned_bar(ui.colours);
  mux::ui::shown_model model(mux::ui::shown_root{});
  skiff::bind::Binding<mux::ui::shown_model> binding;
  binding.refresh(bar, model);
  EXPECT_FALSE(bar.visible());
  mux::ui::show(model, pins::shown{pins::pin_view{"$pinned", "Pinned message", "hello"}});
  binding.refresh(bar, model);
  EXPECT_TRUE(bar.visible());
  mux::ui::show(model, pins::shown{});
  binding.refresh(bar, model);
  EXPECT_FALSE(bar.visible());
}
