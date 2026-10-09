// SPDX-License-Identifier: AGPL-3.0-only
// The program: themes, appearance, rendering and storage.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

void app::rebuild_in_theme() {
  auto& before = root().main();
  const float side_width = before.side_width;
  const float info_width = before.info_width;
  const float settings_at = root().settings_up() ? root().settings_up()->offset() : 0.0f;
  mux::ui::use_scroll_bars(this->appearance().theme);
  colours = mux::ui::palette_of(this->appearance().theme, this->appearance().accent, shared.looks.window.opacity);
  accounts_screen.forget_login();
  shared.drawer_waits = false;
  root().rebuild();
  // The new tree has not read the saved settings or chats, even when the
  // models' revisions match what the old tree last read.
  window_binding.invalidate();
  chats_binding.invalidate();
  // Rebuild the page that was already open; a look change from a space's
  // menu must not open Settings or switch an unrelated settings page.
  mux::ui::change_shown<std::optional<mux::ui::settings_facts>>(showing, [&](auto& now) {
    if (now)
      spl::visit(spl::overloaded{
          [&](mux::ui::settings_page::appearance& page) {
            page.theme = this->appearance().theme;
            page.accent = this->appearance().accent;
          },
          [](auto&) {}}, now->page);
  });
  // The dialogs made again: each reads what is shown afresh.
  showing_binding.invalidate();
  this->refresh_shown();
  auto& after = root().main();
  after.side_width = side_width;
  after.info_width = info_width;
  this->refresh();
  if (auto* up = root().settings_up())
    up->keep_offset(settings_at);
}

}  // namespace mux::app
