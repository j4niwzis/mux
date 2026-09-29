// SPDX-License-Identifier: AGPL-3.0-only
// The program: messages sent and the motion setting.
module mux.app.program;

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

namespace mux::app {

void app::apply(const request::switch_account& one) {
  auto& screen = root().main();
  screen.current = mux::account_id{mux::ui::protocol_of(one.address), one.address};
  screen.chosen.reset();
  root().close_drawer();
  this->refresh();
}

void app::apply(const request::pop_panel&) {
  pending_login.reset();
  root().back_panel();
  this->refresh();
}

void app::apply(const request::settings_home&) {
  if (auto* up = root().settings_up())
    up->show_home();
}

void app::apply(const request::settings_animations&) {
  if (auto* up = root().settings_up())
    up->show_animations();
}

void app::set_motion(std::string level) {
  skiff::paint::motionLevel() = motion_of(level);
  root().show_motion(level);
  motion = std::move(level);
  (void)this->write();
}

}  // namespace mux::app
