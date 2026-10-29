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
import mux.kept_root;
import mux.media;
import mux.ui;
import mux.ui.proto;
import skiff.platform.dialogs;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class looks_part {
 public:
  using accounts = mux::ui::accounts_panel;
  looks_part(services& shared, kept_settings& kept, const mux::ui::palette& colours) : s_(&shared), k_(&kept), colours_(&colours) {}
  looks_part(const looks_part&) = delete;
  looks_part& operator=(const looks_part&) = delete;

  // The looks in effect, as what is kept says: what each level holds of the
  // looks and of room events -- every chat's, the account's and the chat's
  // that settings are open for -- for the choices to show what is in effect;
  // the panels' look for the chat chosen, the window repainted where it
  // changes; and the background behind the window -- the chat's, else every
  // chat's. Put where they moved.
  void read() {
    const mux::chat_settings reads = k_->reads();
    const auto& looks = k_->appearance();
    const auto& history = k_->history();
    const auto& chosen = s_->showing->look<mux::ui::chat_shown>()->chosen;
    const std::optional<mux::conversation_id> managed = s_->managed();
    auto& shown = s_->looks;
    shown.at(mux::choice_level::everywhere{}) = {looks.wallpaper, looks.bubbles, looks.panels};
    mux::ui::room_events_picked events{.everywhere = {history.show_room_events, history.room_event_kinds}};
    const mux::config::account_t* account = managed ? reads.settings_of(managed->account.address) : nullptr;
    shown.at(mux::choice_level::account{}) =
        account ? mux::ui::looks_held{mux::config::wallpaper_of(*account).transform([](const std::string& word) { return mux::config::wallpaper_of(std::string_view(word)); }),
                                      mux::config::bubbles_of(*account).transform([](const std::string& word) { return mux::config::bubble_look_of(word); }),
                                      mux::config::panels_of(*account).transform([](const std::string& word) { return mux::config::bubble_look_of(word); })}
                : mux::ui::looks_held{};
    if (account)
      events.account = {mux::config::room_events_of(*account), mux::config::room_event_kinds_of(*account)};
    shown.at(mux::choice_level::chat{}) =
        managed ? mux::ui::looks_held{reads.own_of<&mux::config::chat_choices::wallpaper>(*managed), reads.own_of<&mux::config::chat_choices::bubbles>(*managed),
                                      reads.own_of<&mux::config::chat_choices::panels>(*managed)}
                : mux::ui::looks_held{};
    if (managed)
      events.chat = {reads.own_of<&mux::config::chat_choices::room_events>(*managed), reads.own_of<&mux::config::chat_choices::room_event_kinds>(*managed)};
    shown.panels = chosen ? reads.panels_of(*chosen) : looks.panels.value_or(mux::config::bubble_look{});
    if (mux::ui::show_panels(s_->paint, shown.panels, shown.window, *colours_))
      s_->scene->repaintAll();
    const auto wallpaper = chosen ? reads.wallpaper_of(*chosen) : looks.wallpaper.value_or(mux::config::wallpaper_t{mux::config::wallpaper::theme{}});
    this->put(mux::ui::behind_shown{wallpaper});
    this->put(mux::ui::chat_backdrop_shown{wallpaper, shown.window.behind, static_cast<float>(shown.window.opacity) / 100.0f});
    this->put(std::move(events));
    mux::ui::show_looks(*s_->showing, shown);
  }
  // Which room events show at a level, set to a way: Custom from what was
  // in effect there.
  void apply(const request::set_events_way& one) {
    const auto held = mux::ui::events_for_way(*s_->showing->look<mux::ui::room_events_picked>(), one.level, one.way);
    spl::visit(spl::overloaded{[&](mux::choice_level::everywhere) {
                                 auto now = k_->history();
                                 mux::ui::events_in(now, held);
                                 k_->set_part(std::move(now));
                               },
                               [&](mux::choice_level::account) {
                                 s_->with_chosen_account([&](mux::config::account_t& account) {
                                   mux::config::room_events_in(account) = held.all;
                                   mux::config::room_event_kinds_in(account) = held.kinds;
                                 });
                               },
                               [&](mux::choice_level::chat) {
                                 if (const auto chat = s_->managed()) {
                                   k_->choose<&mux::app::chat_choices::room_events>(*chat, held.all);
                                   k_->choose<&mux::app::chat_choices::room_event_kinds>(*chat, held.kinds);
                                 }
                               }},
               one.level);
    (void)k_->write();
    s_->refresh_due = true;
  }
 private:
  template <class Part>
  void put(Part now) {
    if (*s_->showing->look<Part>() != now)
      mux::ui::show(*s_->showing, std::move(now));
  }

  services* s_;
  kept_settings* k_;
  const mux::ui::palette* colours_;
};

}  // namespace mux::app
