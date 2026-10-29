// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.manage: a space shown as a forum or in Home, or not; what is
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

  // A space shown as one chat, its rooms as topics -- or as a space. Not one
  // that holds spaces: it is a space.
  void apply(const request::flip_forum& one) {
    const auto by = s_->listed();
    if (!by)
      return;
    const mux::conversation_id id{*by, one.room};
    const mux::conversation* space = s_->model->find(id);
    if (!space || !space->space)
      return;
    const bool holds_spaces = std::ranges::any_of(space->children, [&](const std::string& child) {
      const mux::conversation* in = s_->model->find(mux::conversation_id{*by, child});
      return in && in->space;
    });
    if (k_->own_of<&mux::app::chat_choices::forum>(id) || !holds_spaces)
      k_->flip<&mux::app::chat_choices::forum>(id);
    (void)k_->write();
    s_->refresh_due = true;
  }
  // A space's rooms in Home, or not: its own choice.
  void apply(const request::flip_home_hide& one) {
    const auto by = s_->listed();
    if (!by)
      return;
    const mux::conversation_id id{*by, one.room};
    const mux::conversation* space = s_->model->find(id);
    if (!space || !space->space)
      return;
    k_->flip<&mux::app::chat_choices::hidden_from_home>(id);
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::close_forum&) {
    mux::ui::change_shown<mux::ui::chat_shown>(*s_->showing, [](mux::ui::chat_shown& now) { now.forum.reset(); });
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
