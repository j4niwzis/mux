// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.services: what every part of the program works with -- the model,
// the network, the messages on disk, the window, and asking for the window
// to be brought up to date -- handed to each part, and nothing else of the
// others: a part owns its own state, and reaches the rest through this.
export module mux.app.services;

import std;
import skiff.scene;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.store;
import mux.app.requests;

export namespace mux::app {

struct services {
  mux::model* model = nullptr;
  network* net = nullptr;
  message_store* store = nullptr;
  mailbox_type* box = nullptr;
  actions* ask = nullptr;
  skiff::scene::Scene<window_type>* scene = nullptr;
  // The window brought up to date with the model, after a part changed it.
  std::function<void()> refresh;

  [[nodiscard]] window_type& root() const { return scene->root(); }
  // The demo: no network, and nothing kept.
  [[nodiscard]] bool demo() const { return ask->demo; }
};

}  // namespace mux::app
