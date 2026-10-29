// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.looks.app -- The looks' part of the program: a chat's
// background and the bubbles' and panels' look set at a level, in its own
// settings -- every
// chat's, the chosen account's (on its page), or the chat's own -- as the
// level over it says, the theme's, plain, or a picture chosen, copied into
// mux's data so that it stays. The looks in effect are read by the
// program's own looks part, as what is kept moves.
export module mux.plugin.looks.app;

import std;
import splice;
import splice.bytes;
import skia;
import mux.composition;
import mux.core;
import mux.config;
import mux.media;
import mux.platform.files;
import mux.ui;
import skiff.platform.dialogs;
import mux.app.kept;
import mux.app.services;
import mux.plugin.looks.types;
import mux.plugin.looks.ui_top;

export namespace mux::plugins::looks {

struct part {
  app::services* s_ = nullptr;
  // A picture being chosen for a background: at which level.
  std::optional<choice_level_t> picking_;

  // A background chosen: the theme's, plain, or as the level over it says,
  // set at once; a picture, chosen first in the system's dialog (took_files).
  void apply(const ui::request::set_wallpaper& one) {
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
  // A look's choice -- a kind, a slider let go, an element given back --
  // worked out from the looks as they are, and set; nothing where it does
  // nothing.
  void apply(const ui::request::change_bubbles& one) {
    if (auto asked = bubbles_asked(mux::ui::looks_picked_of(s_->looks), one))
      this->apply(*asked);
  }
  // Bubbles or panels, at a level: a look, or as the level over it says.
  void apply(const ui::request::set_bubbles& one) {
    const auto word = one.look ? std::optional<std::string>(mux::config::word_of(*one.look)) : std::nullopt;
    const bool set = this->change_at(one.level, [&](words& level) {
      spl::visit(spl::overloaded{[&](mux::config::look_part::bubbles) { level.bubbles = word; },
                                 [&](mux::config::look_part::panels) { level.panels = word; }},
                 one.part);
    });
    if (!set)
      return;
    // Every chat's, where the window reads it, at once.
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                 spl::visit(spl::overloaded{[&](mux::config::look_part::bubbles) {
                                                              s_->looks.bubbles_everywhere = one.look.value_or(mux::config::bubble_look{});
                                                            },
                                                            [&](mux::config::look_part::panels) {
                                                              s_->looks.panels_everywhere = one.look.value_or(mux::config::bubble_look{});
                                                            }},
                                            one.part);
                               },
                               [](const auto&) {}},
               one.level);
  }

  // A level's words changed, in the plugin's own settings: every chat's;
  // the account's whose page is open; the chat Manage is for. Kept, and
  // the window shown them. Whether there was such a level.
  template <class Change>
  bool change_at(const mux::choice_level_t& level, Change change) {
    auto now = s_->kept->plugin_settings<kept>();
    const bool there = spl::visit(
        spl::overloaded{[&](mux::choice_level::everywhere) {
                          change(now.everywhere);
                          return true;
                        },
                        [&](mux::choice_level::account) {
                          const auto chosen = mux::ui::chosen_of(*s_->showing->look<std::optional<mux::ui::panel_facts>>());
                          if (!chosen)
                            return false;
                          change(now.accounts[std::string(*chosen)]);
                          return true;
                        },
                        [&](mux::choice_level::chat) {
                          const auto chat = s_->managed();
                          if (!chat)
                            return false;
                          change(chat_at(now, *chat));
                          return true;
                        }},
        level);
    if (!there)
      return false;
    // A level that says nothing is not kept.
    std::erase_if(now.accounts, [](const auto& one) { return one.second == words{}; });
    std::erase_if(now.chats, [](const chat_words& one) { return one.look == words{}; });
    s_->kept->set_plugin_settings(std::move(now));
    (void)s_->kept->write();
    s_->refresh_due = true;
    return true;
  }

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
  // A background set at a level -- none: as the level over it says -- kept;
  // its choices read it as the looks move (looks_picked).
  void set_at(const mux::choice_level_t& level, std::optional<mux::config::wallpaper_t> chosen) {
    (void)this->change_at(level, [&](words& at) {
      at.wallpaper = chosen ? std::optional<std::string>(mux::config::word_of(*chosen)) : std::nullopt;
    });
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::looks
