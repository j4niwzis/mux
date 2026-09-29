// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.settings: the settings box and what its pages change -- the
// theme and the accent, the renderer, how much moves, how much is kept and
// its clearing, and what is done to files sent -- each change written to
// the accounts file.
export module mux.app.settings;

import std;
import skiff.paint;
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
  settings_part(services& shared, kept_settings& kept, pictures_part& pictures, std::function<void()> rebuild)
      : s_(&shared), k_(&kept), pictures_(&pictures), rebuild_(std::move(rebuild)) {}

  // The limits, as kept: to the caches and the disk.
  void apply_limits() {
    pictures_->set_limits(k_->limits);
    s_->store->budget = static_cast<std::uintmax_t>(k_->limits.messages_on_disk_mb) << 20;
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
      up->show_appearance(k_->theme, k_->accent);
  }
  void apply(const request::settings_rendering&) {
    if (auto* up = s_->root().settings_up())
      up->show_rendering(k_->renderer);
  }
  void apply(const request::set_theme& one) {
    k_->theme = one.theme;
    (void)k_->write();
    rebuild_();
  }
  void apply(const request::set_accent& one) {
    k_->accent = one.accent;
    (void)k_->write();
    rebuild_();
  }
  // The renderer: for the next start, kept.
  void apply(const request::set_renderer& one) {
    k_->renderer = one.renderer;
    if (auto* up = s_->root().settings_up()) {
      if (auto* page = up->appearance())
        page->show(k_->theme, k_->accent);
      if (auto* page = up->rendering())
        page->show(k_->renderer);
    }
    (void)k_->write();
  }

  // Storage: how much is kept, halved or doubled within its bounds; and all
  // of it cleared.
  void apply(const request::settings_storage&) {
    if (auto* up = s_->root().settings_up())
      up->show_storage(k_->limits, k_->history);
  }
  void apply(const request::change_limit& one) {
    std::int64_t& value = mux::config::value_of(k_->limits, one.which);
    const auto [low, high] = mux::config::bounds_of(one.which);
    value = std::clamp(one.more ? value * 2 : value / 2, low, high);
    this->apply_limits();
    s_->model->trim(static_cast<std::size_t>(k_->limits.messages_in_memory), s_->root().main().chosen);
    (void)k_->write();
    if (auto* up = s_->root().settings_up())
      if (auto* page = up->storage())
        page->show(k_->limits);
  }
  void apply(const request::clear_stored&) {
    std::error_code failed;
    std::filesystem::remove_all(mux::config::state_path("messages"), failed);
    pictures_->clear();
    s_->root().show_message("Storage", "The stored messages and pictures are cleared.");
    s_->refresh();
  }

  // Files sent: their metadata cut out, their names made plain.
  void apply(const request::settings_files&) {
    if (auto* up = s_->root().settings_up())
      up->show_files(k_->sending);
  }
  // Deleted messages kept where they were, marked, or taken out.
  void apply(const request::flip_keep_deleted&) {
    k_->history.keep_deleted = !k_->history.keep_deleted;
    s_->model->keep_deleted = k_->history.keep_deleted;
    (void)k_->write();
  }
  void apply(const request::flip_strip_metadata&) {
    k_->sending.strip_metadata = !k_->sending.strip_metadata;
    (void)k_->write();
  }
  void apply(const request::flip_rename_pictures&) {
    k_->sending.rename = !k_->sending.rename;
    (void)k_->write();
  }

 private:
  services* s_;
  kept_settings* k_;
  pictures_part* pictures_;
  std::function<void()> rebuild_;
};

}  // namespace mux::app
