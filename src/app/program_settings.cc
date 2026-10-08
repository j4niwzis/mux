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
  const float side_width = before.side_width;
  const float info_width = before.info_width;
  const float settings_at = root().settings_up() ? root().settings_up()->offset() : 0.0f;
  mux::ui::use_scroll_bars(this->appearance().theme);
  colours = mux::ui::palette_of(this->appearance().theme, this->appearance().accent, shared.looks.window.opacity);
  accounts_screen.forget_login();
  shared.drawer_waits = false;
  root().rebuild();
  // The dialogs made again: each reads what is shown afresh.
  showing_binding.invalidate();
  this->refresh_shown();
  auto& after = root().main();
  after.side_width = side_width;
  after.info_width = info_width;
  this->refresh();
  mux::ui::show(showing, std::optional(mux::ui::settings_facts{mux::ui::settings_page::appearance{this->appearance().theme, this->appearance().accent}}));
  this->refresh_shown();
  if (auto* up = root().settings_up())
    up->keep_offset(settings_at);
}

}  // namespace mux::app
