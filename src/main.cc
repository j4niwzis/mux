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
import mux.vault;
import std;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.platform.window;
import mux.platform.events;
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

int mux_main(int argc, char** argv) {
  // The window's own kinds of event, registered once, here.
  const mux::platform::events::kinds kinds;
  mailbox_type box{wake_window{kinds.wake}};
  mux::model model;
  // What is kept on disk is read and written through this one: placed and
  // unlocked here, before anything is read, and handed to all that keeps.
  mux::vault::vault vault;
  network net;
  net.box = &box;
  net.vault = &vault;

  // `mux --demo`: fake accounts and conversations, no network, nothing kept.
  const bool demo = argc > 1 && std::string_view(argv[1]) == "--demo";
  const std::filesystem::path config_path = mux::config::default_path();
  // Local data encrypted, where the user turned it on: its header beside the
  // settings, and the vault unlocked before anything is read.
  vault.place(config_path.parent_path() / "vault.json");
  // Its directories the user's alone, whether the vault is on or not.
  if (!demo)
    for (const auto& dir : {config_path.parent_path(), mux::config::state_path("").parent_path(),
                            mux::config::cache_path("").parent_path()})
      mux::vault::vault::keep_private(dir);
  mux::config::file saved;
  std::optional<std::string> config_error;
  std::optional<std::string> config_note;
  if (demo) {
    saved = mux::config::file_of(fake::accounts());
    fake::fill(model);
  } else if (vault.locked()) {
    // Read once the vault is opened.
  } else if (vault.sealed_without_header(config_path)) {
    // Sealed, and vault.json gone: never put aside as unreadable nor written
    // over -- nothing is changed, nothing started, until it is put back.
    config_error = "Local data here is encrypted, but vault.json, beside the settings, is gone. Put it back and "
                   "start mux again; nothing is changed until then.";
  } else if (auto loaded = mux::config::load(config_path, vault))
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

  // The window's look, then the theme: what is made takes its colours from
  // them. Its opacity is as it was at the start for all of the run -- the
  // window is made see-through or not once.
  const int opacity = std::clamp(saved.window_opacity.value_or(100), 20, 100);
  const mux::ui::window_look_t window{.opacity = opacity, .chosen = opacity, .behind = saved.wallpaper_behind.value_or(false),
                            .see_through = opacity < 100,
                            .frost = std::clamp(saved.frost ? *saved.frost : saved.frost_blur ? static_cast<double>(*saved.frost_blur) / 3.0 : 10.0, 0.0, 100.0)};
  mux::ui::use_scroll_bars(mux::config::theme_of(saved.theme));
  // The application owns the UI tree inline, which exceeds Android's
  // native activity thread stack. Use static storage, but destroy it before
  // the local model, vault and network that it borrows go out of scope.
  static std::optional<app> program_storage;
  struct reset_program {
    std::optional<app>& storage;
    ~reset_program() { storage.reset(); }
  } program_lifetime{program_storage};
  auto& program = program_storage.emplace(
      mux::ui::palette_of(mux::config::theme_of(saved.theme), mux::config::accent_of(saved.accent), opacity), window);
  program.box = &box;
  program.wake = wake_window{kinds.wake};
  program.vault = &vault;
  program.model = &model;
  program.net = &net;
  program.ask.net = &net;
  program.ask.demo = demo;
  program.ask.box = &box;
  program.wire();
  program.config_path = config_path;
  // Encrypted local data, locked: the window comes up on the unlock screen,
  // and what is kept is read once it is opened (app::unlock). Else at once.
  if (vault.locked())
    program.lock(std::move(extra), demo);
  else
    program.begin(saved, std::move(extra), demo || vault.sealed_without_header(config_path),
                  std::move(config_error));

  if (config_note)
    program.root().show_message("The accounts file could not be read", *config_note);
  const int code = mux::platform::window::run(
      program, {.software = program.appearance().renderer == mux::config::renderer_t{mux::config::renderer::software{}},
                .transparent = opacity < 100}, kinds);
  // Started only once what is kept was read: never, where the vault stayed locked.
  if (net.thread.joinable())
    net.thread.join();
  return code;
}
