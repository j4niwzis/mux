// SPDX-License-Identifier: AGPL-3.0-only
// The program: the account forms, adding and editing accounts.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
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

void app::switch_form(const mux::protocol_t& speaks) {
  auto* up = root().open_panel();
  if (!up)
    return;
  pending_login.reset();
  splice::visit(
      [&](accounts& panel) {
        if (auto* pane = panel.adding())
          pane->show(speaks);
      },
      *up);
}

void app::flip_enabled(const std::string& address) {
  const auto found = this->find(address);
  if (found == saved.end())
    return;
  bool& enabled = mux::config::enabled_of(*found);
  enabled = !enabled;
  if (enabled)
    net->add(*found, proxies);
  else
    net->remove(address);
  this->save_from_accounts();
  this->refresh();
}

void app::remove(const std::string& address) {
  if (std::erase_if(saved, [&](const auto& one) { return mux::config::address_of(one) == address; }) == 0)
    return;
  net->remove(address);
  this->save_from_accounts();
  this->refresh();
}

void app::save_from_accounts() {
  if (auto failed = this->write())
    if (auto* up = root().open_panel())
      splice::visit([&](accounts& panel) { panel.say(*failed); }, *up);
}

}  // namespace mux::app
