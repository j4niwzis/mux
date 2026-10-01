// SPDX-License-Identifier: AGPL-3.0-only
// mux: the program. The network half runs on a thread of its own -- every
// account a fiber on one loop -- and the window on the main thread; what the
// accounts say crosses to the window in a mailbox, and what the window asks
// for is posted back to the loop.
//
//   mux                   the accounts saved in ~/.config/mux/accounts.json
//   mux <account>...      and these too, for this run, with the password in
//                         MUX_PASSWORD: user@domain for XMPP, @user:server for
//                         Matrix
//   mux --demo            fake accounts and conversations: no network, and
//                         nothing kept
//
// With no accounts at all it opens all the same, and says how to add one.
import std;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
import skiff.paint;
import skiff.scene;

import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.app.program;

namespace {

using namespace mux::app;


}  // namespace

int main(int argc, char** argv) {
  mailbox_type box{wake_window{}};
  mux::model model;
  network net;
  net.box = &box;

  // `mux --demo`: fake accounts and conversations, no network, nothing kept.
  const bool demo = argc > 1 && std::string_view(argv[1]) == "--demo";
  const std::filesystem::path config_path = mux::config::default_path();
  mux::config::file saved;
  std::optional<std::string> config_error;
  std::optional<std::string> config_note;
  if (demo) {
    saved = mux::config::file_of(fake::accounts());
    fake::fill(model);
  } else if (auto loaded = mux::config::load(config_path))
    saved = std::move(*loaded);
  else {
    // Not a file this mux can read -- one of an older mux, most likely: kept
    // aside, where it can be looked at, and a new one begun, so that what is
    // set from now on is saved. Refusing to save instead lost every change
    // without a word.
    std::filesystem::path aside = config_path;
    aside += ".unreadable";
    std::error_code moved;
    std::filesystem::rename(config_path, aside, moved);
    if (moved) {
      config_error = loaded.error();
    } else {
      config_note = std::format("{}\n\nIt was kept as {}, and a new one begun.", loaded.error(), aside.string());
    }
    std::println(std::cerr, "[mux] {}", loaded.error());
  }

  // Accounts named on the command line, for this run only.
  std::vector<mux::config::account_t> extra;
  if (argc > 1 && !demo) {
    const char* password = std::getenv("MUX_PASSWORD");
    if (!password) {
      std::println(std::cerr, "accounts on the command line take their password from MUX_PASSWORD, which is not set");
      return 2;
    }
    for (int at = 1; at < argc; ++at)
      extra.push_back(mux::config::account_from(argv[at], password));
  }

  const auto proxies = saved.proxies.value_or(std::vector<mux::config::proxy_settings>{});
  for (const auto& one : mux::config::accounts_of(saved))
    if (mux::config::enabled_of(one) && !demo)
      net.start(one, proxies);
  for (const auto& one : extra)
    net.start(one, proxies);
  net.thread = std::thread([&net] {
    try {
      net.loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(std::cerr, "[mux] the network stopped: {}", failed.what());
    }
  });

  // The window's look, then the theme: what is made takes its colours from
  // them. Its opacity is as it was at the start for all of the run -- the
  // window is made see-through or not once.
  const int opacity = std::clamp(saved.window_opacity.value_or(100), 20, 100);
  mux::ui::window_look() = {.opacity = opacity, .chosen = opacity, .behind = saved.wallpaper_behind.value_or(false),
                            .see_through = opacity < 100,
                            .frost = std::clamp(saved.frost ? *saved.frost : saved.frost_blur ? static_cast<double>(*saved.frost_blur) / 3.0 : 10.0, 0.0, 100.0)};
  mux::ui::use_theme(mux::config::theme_of(saved.theme), mux::config::accent_of(saved.accent));
  app program;
  program.box = &box;
  program.model = &model;
  program.net = &net;
  program.ask.net = &net;
  program.ask.demo = demo;
  program.ask.box = &box;
  program.wire();
  program.config_path = config_path;
  program.keeps_nothing = demo;
  program.saved = mux::config::accounts_of(saved);
  program.motion = saved.motion;
  // The account shown last, shown again once it is in the model: accounts
  // arrive after the first frame, and the first one there is not the one.
  program.last_account = saved.last_account;
  // The emoji picked lately: shown first in the panels, and kept as picked.
  program.recent_emoji = saved.recent_emoji.value_or(std::vector<std::string>{});
  mux::ui::recent_emoji() = program.recent_emoji;
  // And the stickers sent lately, and the favourites.
  const auto emotes_of = [](const std::optional<std::vector<mux::config::sticker_kept>>& kept) {
    return kept.value_or(std::vector<mux::config::sticker_kept>{}) | std::views::transform([](const mux::config::sticker_kept& one) {
             return mux::emote{.shortcode = one.shortcode, .url = one.url, .body = one.body, .w = one.w, .h = one.h, .size = one.size,
                               .mimetype = one.mimetype};
           }) |
           std::ranges::to<std::vector>();
  };
  program.recent_stickers = emotes_of(saved.recent_stickers);
  program.favourite_stickers = emotes_of(saved.favourite_stickers);
  mux::ui::recent_stickers() = program.recent_stickers;
  mux::ui::favourite_stickers() = program.favourite_stickers;
  if (saved.last_account)
    program.root().main().wanted = mux::account_id{mux::ui::protocol_of(*saved.last_account), *saved.last_account};
  program.theme = mux::config::theme_of(saved.theme);
  if (saved.wallpaper)
    program.wallpaper = mux::config::wallpaper_of(std::string_view(*saved.wallpaper));
  if (saved.bubbles)
    program.bubbles = mux::config::bubble_look_of(*saved.bubbles);
  mux::ui::bubble_look_everywhere() = program.bubbles.value_or(mux::config::bubble_look{});
  if (saved.panels)
    program.panels = mux::config::bubble_look_of(*saved.panels);
  mux::ui::panel_look_everywhere() = program.panels.value_or(mux::config::bubble_look{});
  program.accent = mux::config::accent_of(saved.accent);
  program.renderer = mux::config::renderer_of(saved.renderer);
  program.partial_redraw = saved.partial_redraw.value_or(false);
  program.flash_redraws = saved.flash_redraws.value_or(false);
  program.vsync = saved.vsync.value_or(true);
  program.window_opacity = opacity;
  program.wallpaper_behind = mux::ui::window_look().behind;
  mux::ui::window_look().live_blur = saved.live_blur.value_or(false);
  program.live_blur = mux::ui::window_look().live_blur;
  program.frost_blur = mux::ui::window_look().frost;
  program.spaces = saved.spaces.value_or(true);
  program.top_bar = saved.top_bar.value_or(true);
  program.home_hides_spaced = saved.home_hides_spaced.value_or(false);
  mux::ui::window_look().home_hides = program.home_hides_spaced;
  program.home_hides_direct = saved.home_hides_direct.value_or(false);
  mux::ui::window_look().home_direct = program.home_hides_direct;
  mux::ui::window_look().spaces = program.spaces;
  mux::ui::window_look().top_bar = program.top_bar;
  if (saved.space_places)
    program.space_places = *saved.space_places | std::views::transform([](const mux::config::space_place& one) {
                             return mux::config::space_placed{one.account, mux::config::space_item_of(one.item),
                                                              mux::config::space_bar_of(one.bar)};
                           }) |
                           std::ranges::to<std::vector>();
  program.show_fps = saved.show_fps.value_or(false);
  program.limits = saved.cache.value_or(mux::config::cache_limits{});
  if (!demo)
    program.drafts.load();
  program.sending = saved.sending.value_or(mux::config::sending_settings{});
  program.history = saved.history.value_or(mux::config::history_settings{});
  program.model->show_deleted = program.history.show_deleted;
  program.settings.apply_limits();
  program.proxies = proxies;
  program.load_marks();
  program.notifications = saved.notifications.value_or(mux::config::notification_settings{});
  for (const auto& one : saved.chat_notify.value_or(std::vector<mux::config::chat_notify>{}))
    program.notify_modes.insert_or_assign(
        mux::conversation_id{{mux::ui::protocol_of(one.account), one.account}, one.conversation},
        mux::config::notify_mode_of(one.mode));
  for (const auto& one : saved.room_events.value_or(std::vector<mux::config::room_events_choice>{})) {
    const mux::conversation_id chat{{mux::ui::protocol_of(one.account), one.account}, one.conversation};
    if (one.show)
      program.room_events.insert_or_assign(chat, *one.show);
    if (one.kinds)
      program.room_event_kinds.insert_or_assign(chat, *one.kinds);
    if (one.receipts)
      program.receipts_shown_in.insert_or_assign(chat, *one.receipts);
    if (one.previews)
      program.previews_shown_in.insert_or_assign(chat, *one.previews);
    if (one.typing)
      program.typing_sent_in.insert_or_assign(chat, *one.typing);
    if (one.previews_direct)
      program.previews_direct_in.insert_or_assign(chat, *one.previews_direct);
    if (one.jump_search)
      program.jump_search_in.insert_or_assign(chat, *one.jump_search);
    if (one.wallpaper)
      program.wallpaper_in.insert_or_assign(chat, mux::config::wallpaper_of(std::string_view(*one.wallpaper)));
    if (one.bubbles)
      program.bubbles_in.insert_or_assign(chat, mux::config::bubble_look_of(*one.bubbles));
    if (one.panels)
      program.panels_in.insert_or_assign(chat, mux::config::bubble_look_of(*one.panels));
    if (one.forum.value_or(false))
      program.forums.insert(chat);
    if (one.hide_from_home.value_or(false))
      program.hidden_from_home.insert(chat);
  }
  program.placements = saved.placements.value_or(std::vector<mux::config::chat_placement>{});
  for (const auto& one : saved.muted.value_or(std::vector<mux::config::muted_chat>{}))
    program.muted.insert({{mux::ui::protocol_of(one.account), one.account}, one.conversation});
  skiff::paint::motionLevel() = motion_of(saved.motion);
  program.root().show_motion(saved.motion.value_or("full"));
  program.config_error = std::move(config_error);
  program.refresh();

  if (config_note)
    program.root().show_message("The accounts file could not be read", *config_note);
  const int code = mux::host::run(
      program, {.software = program.renderer == mux::config::renderer_t{mux::config::renderer::software{}},
                .transparent = opacity < 100});
  net.thread.join();
  return code;
}
