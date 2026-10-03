// SPDX-License-Identifier: AGPL-3.0-only
// The program: messages sent and the motion setting.
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

void app::apply(const request::switch_account& one) {
  auto& screen = root().main();
  screen.current = mux::account_id{mux::ui::protocol_of(one.address), one.address};
  screen.wanted.reset();
  screen.chosen.reset();
  this->last_account = one.address;
  (void)this->write();
  root().close_drawer();
  this->refresh();
}

void app::apply(const request::pop_panel&) {
  pending_login.reset();
  root().back_panel();
  this->refresh();
}

}  // namespace mux::app
