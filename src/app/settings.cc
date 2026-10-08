// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.settings: the settings box and what its pages change -- the
// theme and the accent, the renderer, how much moves, how much is kept and
// its clearing, and what is done to files sent -- each change written to
// the accounts file.
export module mux.app.settings;

import std;
import mux.vault;
import skiff.paint;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.store;
import mux.app.requests;
import mux.app.services;
import mux.app.kept;
import mux.app.pictures;
import mux.app.words;

export namespace mux::app {

class settings_part {
 public:
  // `rebuild`: the window made anew in the theme, as the program keeps it.
  settings_part(services& shared, kept_settings& kept, pictures_part& pictures)
      : s_(&shared), k_(&kept), pictures_(&pictures) {}

  // The limits, as kept: to the caches and the disk.
  void apply_limits() {
    pictures_->set_limits(k_->limits());
    s_->store->budget = static_cast<std::uintmax_t>(k_->limits().messages_on_disk_mb) << 20;
    s_->store->deleted_budget = static_cast<std::uintmax_t>(mux::config::deleted_on_disk_of(k_->limits())) << 20;
  }
  // How much moves: set, shown, kept.
  void set_motion(std::string level) {
    skiff::paint::motionLevel() = motion_of(level);
    s_->root().show_motion(level);
    k_->motion = std::move(level);
    (void)k_->write();
  }

  void apply(const request::open_settings&) {
    s_->root().close_drawer();
    s_->root().open_settings(k_->motion.value_or("full"));
  }
  void apply(const request::close_settings&) { s_->root().close_settings(); }
  void apply(const request::settings_home&) {
    if (auto* up = s_->root().settings_up())
      up->show_home();
  }
  void apply(const request::settings_animations&) {
    if (auto* up = s_->root().settings_up())
      up->show_animations();
  }
  void apply(const request::set_motion& one) { this->set_motion(one.level); }

  // Appearance and rendering.
  void apply(const request::settings_appearance&) {
    if (auto* up = s_->root().settings_up())
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
  }
  void apply(const request::settings_rendering&) {
    if (auto* up = s_->root().settings_up())
      up->show_rendering(k_->appearance().renderer);
  }
  // Frosted's blur: kept, the backgrounds' frost made again, and shown.
  void apply(const request::set_frost_blur& one) {
    k_->choose_field<&mux::config::look_settings::frost_blur>(std::clamp(one.percent, 0.0, 100.0));
    s_->looks.window.frost = k_->appearance().frost_blur;
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
  }
  void apply(const request::set_renderer& one) {
    k_->choose_field<&mux::config::look_settings::renderer>(one.renderer);
    if (auto* up = s_->root().settings_up()) {
      if (auto* page = up->rendering())
        page->show(k_->appearance().renderer);
    }
    (void)k_->write();
  }

  // Storage: how much is kept, halved or doubled within its bounds; and all
  // of it cleared.
  void apply(const request::settings_storage&) {
    if (auto* up = s_->root().settings_up())
      up->show_storage(k_->limits(), k_->history(), s_->vault->on());
  }
  void apply(const request::clear_stored&) {
    std::error_code failed;
    std::filesystem::remove_all(mux::config::state_path("messages"), failed);
    pictures_->clear();
    s_->root().show_message("Storage", "The stored messages and pictures are cleared.");
    s_->refresh_due = true;
  }

  // Files sent: their metadata cut out, their names made plain.
  void apply(const request::settings_files&) {
    if (auto* up = s_->root().settings_up())
      up->show_files(k_->sending());
  }
  // Room events, for every chat that has not chosen, nor its account.
  void apply(const request::flip_room_events&) {
    k_->choose_field<&mux::config::history_settings::show_room_events>(!k_->history().show_room_events);
    (void)k_->write();
    s_->refresh_due = true;
  }

 private:
  services* s_;
  kept_settings* k_;
  pictures_part* pictures_;
};

}  // namespace mux::app
