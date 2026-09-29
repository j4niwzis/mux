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

  // The theme first: what is made takes its colours from it.
  mux::ui::use_theme(mux::config::theme_of(saved.theme), mux::config::accent_of(saved.accent));
  app program;
  program.box = &box;
  program.model = &model;
  program.net = &net;
  // A pill's avatar in a message's text: the one of what it names.
  skiff::scene::pillPicture() = [](std::string_view target) -> std::optional<skiff::scene::PillPicture> {
    const auto at = target.find("#/");
    const std::string_view id = at == std::string_view::npos ? target : target.substr(at + 2);
    const auto [top, bottom] = mux::ui::userpic_colours(id);
    return skiff::scene::PillPicture{mux::ui::avatar_images().find(id), top, bottom, mux::ui::initials_of(id)};
  };
  // A link pressed in a message's text: routed as a link is.
  skiff::scene::linkOpener() = {+[](void* self, std::string_view url) {
                                  static_cast<app*>(self)->ask.open_url(std::string(url));
                                },
                                &program};
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
  mux::ui::on_recent_emoji() = [&program] {
    program.recent_emoji = mux::ui::recent_emoji();
    (void)program.write();
  };
  if (saved.last_account)
    program.root().main().wanted = mux::account_id{mux::ui::protocol_of(*saved.last_account), *saved.last_account};
  program.theme = mux::config::theme_of(saved.theme);
  program.accent = mux::config::accent_of(saved.accent);
  program.renderer = mux::config::renderer_of(saved.renderer);
  program.limits = saved.cache.value_or(mux::config::cache_limits{});
  if (!demo)
    program.drafts.load();
  program.sending = saved.sending.value_or(mux::config::sending_settings{});
  program.history = saved.history.value_or(mux::config::history_settings{});
  program.model->show_deleted = program.history.show_deleted;
  program.settings.apply_limits();
  program.proxies = proxies;
  for (const auto& one : saved.muted.value_or(std::vector<mux::config::muted_chat>{}))
    program.muted.insert({{mux::ui::protocol_of(one.account), one.account}, one.conversation});
  skiff::paint::motionLevel() = motion_of(saved.motion);
  program.root().show_motion(saved.motion.value_or("full"));
  program.config_error = std::move(config_error);
  program.refresh();

  if (config_note)
    program.root().show_message("The accounts file could not be read", *config_note);
  const int code = mux::host::run(
      program, {.software = program.renderer == mux::config::renderer_t{mux::config::renderer::software{}}});
  net.thread.join();
  return code;
}
