// SPDX-License-Identifier: AGPL-3.0-only
// The program: files sent, pictures opened and saved, downloads.
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
import mux.logic.links;

namespace mux::app {

void app::apply(const request::open_url& one) {
  if (const auto where = mux::logic::link_of(one.url)) {
    this->follow(*where);
    return;
  }
  // Only the web's and mail's links go to the system's opener: a link of a
  // message is anyone's to write, and file://, smb://, or a scheme some
  // program registered could open, mount or run what it names (review 5).
  if (!mux::logic::opens_outside(one.url)) {
    root().show_message("Not opened", std::format("mux opens only http, https and mailto links, not:\n{}", one.url));
    return;
  }
  mux::host::open_url(one.url);
}

}  // namespace mux::app
