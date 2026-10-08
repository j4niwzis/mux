// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.looks: a chat's background and the bubbles' and panels' look, at
// a level -- every chat's, the chosen account's (on its page), or the
// chat's own -- as the level over it says, the theme's, plain, or a picture
// chosen, copied into mux's data so that it stays. A part of the program:
// it reaches the settings it is given and the rest through the services.
export module mux.app.looks;

import std;
import mux.platform.files;
import splice.bytes;
import splice;
import skia;
import mux.core;
import mux.config;
import mux.media;
import mux.ui;
import mux.ui.proto;
import mux.platform.dialogs;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class looks_part {
 public:
  using accounts = mux::ui::accounts_panel<actions>;
  looks_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  looks_part(const looks_part&) = delete;
  looks_part& operator=(const looks_part&) = delete;

  void apply(const request::open_wallpaper& one) { mux::ui::show(*s_->showing, std::optional(mux::ui::wallpaper_facts{one.level})); }
  void apply(const request::close_wallpaper&) { mux::ui::show<mux::ui::wallpaper_facts>(*s_->showing, std::nullopt); }
  // A background chosen: the theme's, plain, or as the level over it says,
  // set at once; a picture, chosen first in the system's dialog (took_files).
  void apply(const request::set_wallpaper& one) {
    spl::visit(spl::overloaded{[&](mux::config::wallpaper_pick::inherit) { this->set_at(one.level, std::nullopt); },
                                     [&](mux::config::wallpaper_pick::theme) { this->set_at(one.level, mux::config::wallpaper::theme{}); },
                                     [&](mux::config::wallpaper_pick::plain) { this->set_at(one.level, mux::config::wallpaper::plain{}); },
                                     [&](mux::config::wallpaper_pick::picture) {
                                       picking_ = one.level;
                                       s_->system_dialogs->choose_files();
                                     }},
                  one.pick);
  }
  // Files chosen in the dialog: the background's picture, where one was
  // asked for here. Whether it was.
  bool took_files(const std::vector<std::string>& paths, bool dropped) {
    if (!picking_ || dropped)
      return false;
    const auto level = *std::exchange(picking_, std::nullopt);
    if (!paths.empty())
      this->picture_chosen(level, paths.front());
    return true;
  }
  // Bubbles, at a level: a look, or as the level over it says.
  void apply(const request::set_bubbles& one) {
    // Where each level keeps the look asked for: the bubbles', or the panels'.
    auto& known = spl::visit(
        spl::overloaded{[this](mux::config::look_part::bubbles) -> mux::config::bubble_look& { return s_->looks.bubbles_everywhere; },
                           [this](mux::config::look_part::panels) -> mux::config::bubble_look& { return s_->looks.panels_everywhere; }},
        one.part);
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                       spl::visit(spl::overloaded{[&](mux::config::look_part::bubbles) { k_->choose_field<&mux::config::look_settings::bubbles>(one.look); },
                                                                  [&](mux::config::look_part::panels) { k_->choose_field<&mux::config::look_settings::panels>(one.look); }},
                                                  one.part);
                                       known = one.look.value_or(mux::config::bubble_look{});
                                     },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         auto& kept = spl::visit(
                                             spl::overloaded{[&](mux::config::look_part::bubbles) -> std::optional<std::string>& { return mux::config::bubbles_in(account); },
                                                                [&](mux::config::look_part::panels) -> std::optional<std::string>& { return mux::config::panels_in(account); }},
                                             one.part);
                                         kept = one.look ? std::optional<std::string>(mux::config::word_of(*one.look)) : std::nullopt;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       const auto chosen = s_->managed();
                                       if (!chosen)
                                         return;
                                       spl::visit(spl::overloaded{[&](mux::config::look_part::bubbles) { k_->choose<&mux::app::chat_choices::bubbles>(*chosen, one.look); },
                                                                  [&](mux::config::look_part::panels) { k_->choose<&mux::app::chat_choices::panels>(*chosen, one.look); }},
                                                  one.part);
                                     }},
                  one.level);
    (void)k_->write();
    s_->refresh_due = true;
    // Appearance up: its choice marked again.
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
    if (auto* managing = s_->root().manage_up())
      managing->show_tab(managing->tab);
  }

 private:
  // The picture chosen for a background: copied into mux's data, by a name
  // its bytes give, and set at the level it was chosen for.
  void picture_chosen(const mux::choice_level_t& level, const std::string& path) {
    auto bytes_read = mux::platform::files::read(path);
    if (!bytes_read)
      return;
    std::string bytes = std::move(*bytes_read);
    const auto type = mux::media::picture_of(bytes);
    if (!type || !skia::decodeImage(bytes.data(), bytes.size())) {
      s_->notice("Chat background", "That file is not a picture mux can show.");
      return;
    }
    // Kept under its own name -- shown where it is chosen -- in a folder its
    // bytes name: two pictures of one name are two files.
    const auto folder = mux::config::state_path("wallpapers") / std::format("{:016x}", std::hash<std::string>{}(bytes));
    std::error_code failed;
    std::filesystem::create_directories(folder, failed);
    const std::filesystem::path given = mux::platform::files::name(path);
    const auto kept = folder / (given.empty() ? std::filesystem::path(std::format("picture.{}", mux::media::extension_of(*type))) : given);
    std::ofstream(kept, std::ios::binary) << bytes;
    this->set_at(level, mux::config::wallpaper::picture{kept.string()});
  }
  // A background set at a level -- none: as the level over it says -- kept,
  // and what shows the choice shown again: Appearance, Manage's tab, the
  // account's Chats page.
  void set_at(const mux::choice_level_t& level, std::optional<mux::config::wallpaper_t> chosen) {
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) { k_->choose_field<&mux::config::look_settings::wallpaper>(chosen); },
                                     [&](mux::choice_level::account) {
                                       s_->with_chosen_account([&](accounts&, mux::config::account_t& account) {
                                         mux::config::wallpaper_in(account) =
                                             chosen ? std::optional<std::string>(mux::config::word_of(*chosen)) : std::nullopt;
                                       });
                                     },
                                     [&](mux::choice_level::chat) {
                                       // The chat Manage is for: a space, where its settings are open.
                                       const auto chat = s_->managed();
                                       if (!chat)
                                         return;
                                       k_->choose<&mux::app::chat_choices::wallpaper>(*chat, chosen);
                                     }},
                  level);
    (void)k_->write();
    mux::ui::show<mux::ui::wallpaper_facts>(*s_->showing, std::nullopt);
    s_->refresh_due = true;
    if (auto* up = s_->root().settings_up(); up && up->appearance())
      up->show_appearance(k_->appearance().theme, k_->appearance().accent);
    if (auto* managing = s_->root().manage_up())
      managing->show_tab(managing->tab);
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      if (panel.chats_page())
        panel.show_page(mux::ui::account_page::chats{}, account, *s_->model, k_->proxies(), k_->appearance().theme);
    });
  }

  services* s_;
  kept_settings* k_;
  // A picture being chosen for a background: at which level.
  std::optional<mux::choice_level_t> picking_;
};

}  // namespace mux::app
