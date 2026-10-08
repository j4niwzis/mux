// SPDX-License-Identifier: AGPL-3.0-only
// The program: themes, appearance, rendering and storage.
module mux.app.program;

import std;
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
  const auto chosen = before.chosen;
  const auto current = before.current;
  const float side_width = before.side_width;
  const float info_width = before.info_width;
  const bool info_open = before.info_open;
  const float settings_at = root().settings_up() ? root().settings_up()->offset() : 0.0f;
  mux::ui::use_scroll_bars(this->appearance().theme);
  colours = mux::ui::palette_of(this->appearance().theme, this->appearance().accent, shared.looks.window.opacity);
  accounts_screen.forget_login();
  shared.drawer_waits = false;
  root().rebuild();
  auto& after = root().main();
  after.chosen = chosen;
  after.current = current;
  after.side_width = side_width;
  after.info_width = info_width;
  after.info_open = info_open;
  this->refresh();
  root().open_settings(motion.value_or("full"));
  if (auto* up = root().settings_up()) {
    up->show_appearance(this->appearance().theme, this->appearance().accent);
    up->keep_offset(settings_at);
  }
}

}  // namespace mux::app
