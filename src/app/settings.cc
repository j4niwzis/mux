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
    pictures_->set_limits(k_->limits);
    s_->store->budget = static_cast<std::uintmax_t>(k_->limits.messages_on_disk_mb) << 20;
    s_->store->deleted_budget = static_cast<std::uintmax_t>(mux::config::deleted_on_disk_of(k_->limits)) << 20;
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
    skiff::scene::forgetStyles();  // resolved with the theme before
    s_->rebuild_due = true;
  }
  void apply(const request::set_accent& one) {
    k_->accent = one.accent;
    (void)k_->write();
    skiff::scene::forgetStyles();  // resolved with the accent before
    s_->rebuild_due = true;
  }
  // The renderer: for the next start, kept.
  // Taken at the next frame: the host reads them as it draws.
  void apply(const request::flip_partial_redraw&) {
    k_->choose_field<&mux::config::frame_settings::partial_redraw>(!k_->frames().partial_redraw);
    (void)k_->write();
  }
  void apply(const request::flip_flash_redraws&) {
    k_->choose_field<&mux::config::frame_settings::flash_redraws>(!k_->frames().flash_redraws);
    (void)k_->write();
  }
  void apply(const request::flip_vsync&) {
    k_->choose_field<&mux::config::frame_settings::vsync>(!k_->frames().vsync);
    (void)k_->write();
  }
  // The window's opacity: kept, and shown chosen; in effect from the next
  // start, where the window is made see-through or not.
  void apply(const request::set_window_opacity& one) {
    k_->window_opacity = std::clamp(one.percent, 20, 100);
    auto& look = s_->looks.window;
    look.chosen = k_->window_opacity;
    (void)k_->write();
    // A window made see-through: at once, everything in its colours again.
    if (look.see_through) {
      look.opacity = look.chosen;
      skiff::scene::forgetStyles();
      s_->rebuild_due = true;
    } else if (auto* up = s_->root().settings_up()) {
      up->show_appearance(k_->theme, k_->accent);
    }
  }
  // The interface's scale: kept, and taken by the host at the next frame --
  // all of the window laid out again at it; the page shows it chosen.
  void apply(const request::set_interface_scale& one) {
    k_->interface_scale = std::clamp(one.percent, mux::ui::kScaleLeast, mux::ui::kScaleMost);
    s_->looks.window.interface_scale = k_->interface_scale;
    (void)k_->write();
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  // Frosted popups blurring what is under them, live: repainted with it.
  void apply(const request::flip_live_blur&) {
    k_->live_blur = !k_->live_blur;
    s_->looks.window.live_blur = k_->live_blur;
    (void)k_->write();
    s_->refresh_due = true;
  }
  // The background behind the whole window: everything made again over it.
  void apply(const request::flip_wallpaper_behind&) {
    k_->wallpaper_behind = !k_->wallpaper_behind;
    s_->looks.window.behind = k_->wallpaper_behind;
    (void)k_->write();
    skiff::scene::forgetStyles();
    s_->rebuild_due = true;
  }
  // Frosted's blur: kept, the backgrounds' frost made again, and shown.
  void apply(const request::set_frost_blur& one) {
    k_->frost_blur = std::clamp(one.percent, 0.0, 100.0);
    s_->looks.window.frost = k_->frost_blur;
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  // The space bars at all, and the top one.
  void apply(const request::flip_spaces&) {
    k_->spaces = !k_->spaces;
    s_->looks.window.spaces = k_->spaces;
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  void apply(const request::flip_top_bar&) {
    k_->top_bar = !k_->top_bar;
    s_->looks.window.top_bar = k_->top_bar;
    (void)k_->write();
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->theme, k_->accent);
  }
  void apply(const request::flip_show_fps&) {
    k_->choose_field<&mux::config::frame_settings::show_fps>(!k_->frames().show_fps);
    (void)k_->write();
  }
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
      up->show_storage(k_->limits, k_->history, s_->vault->on());
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
    s_->refresh_due = true;
  }

  // Files sent: their metadata cut out, their names made plain.
  void apply(const request::settings_files&) {
    if (auto* up = s_->root().settings_up())
      up->show_files(k_->sending());
  }
  // Deleted messages shown where they were, marked, or taken out of view.
  void apply(const request::flip_show_deleted&) {
    k_->history.show_deleted = !k_->history.show_deleted;
    s_->model->show_deleted = k_->history.show_deleted;
    if (auto* up = s_->root().settings_up())
      if (auto* page = up->storage())
        page->show_deleted(k_->history.show_deleted);
    (void)k_->write();
  }
  // Room events, for every chat that has not chosen, nor its account.
  void apply(const request::flip_room_events&) {
    k_->history.show_room_events = !k_->history.show_room_events;
    (void)k_->write();
    s_->refresh_due = true;
  }
  void apply(const request::flip_strip_metadata&) {
    k_->choose_field<&mux::config::sending_settings::strip_metadata>(!k_->sending().strip_metadata);
    (void)k_->write();
  }
  void apply(const request::flip_rename_pictures&) {
    k_->choose_field<&mux::config::sending_settings::rename>(!k_->sending().rename);
    (void)k_->write();
  }

 private:
  services* s_;
  kept_settings* k_;
  pictures_part* pictures_;
};

}  // namespace mux::app
