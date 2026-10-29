// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.manage: the list of chats open (a forum) left; what is
// done to a room by one allowed to (a person's card's kick and ban). A part
// of the program: it reaches the settings it is given and the rest through
// the services. (A room's settings themselves are the room_settings
// plugin's.)
export module mux.app.manage;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;
import mux.platform.files;
import skiff.platform.dialogs;
import mux.media;
import skia;

export namespace mux::app {

class manage_part {
 public:
  manage_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  manage_part(const manage_part&) = delete;
  manage_part& operator=(const manage_part&) = delete;

  // Out of the list of chats open (a forum).
  void apply(const request::close_scope&) {
    mux::ui::change_shown<mux::ui::chat_shown>(*s_->showing, [](mux::ui::chat_shown& now) { now.scope.reset(); });
  }
  // Done to the room being read, by its account.
  void apply(const request::room_act& one) {
    const auto chosen = s_->managed();
    if (!chosen || s_->demo())
      return;
    s_->net->manage(*chosen, one.action);
  }

 private:
  services* s_;
  kept_settings* k_;
};

}  // namespace mux::app
