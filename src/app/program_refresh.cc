// SPDX-License-Identifier: AGPL-3.0-only
// The program: the window brought up to date with the settings -- each thing
// shown of them, a function of its own.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

// The space each chat is in: its choices, where the chat has none.
void app::note_spaces() {
  space_above.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations)
      if (one.space)
        for (const std::string& child : one.children)
          if (child != one.id.id)
            space_above.try_emplace(mux::conversation_id{one.id.account, child}, one.id);
}

// The chosen chat's bubbles and the panels' look, as its levels say.
void app::show_looks_now() {
  // The panels' look, as the chosen chat's levels say, else every chat's:
  // the whole window repainted where it changes -- nothing made again.
  shared.looks.panels = root().main().chosen ? this->panels_of(*root().main().chosen)
                                                   : this->appearance().panels.value_or(mux::config::bubble_look{});
  if (mux::ui::show_panels(shared.paint, shared.looks.panels, shared.looks.window, colours)) {
    root().markDamaged();
    skiff::scene::work::mark(root().main().fState.fId);  // an ease ticked by the screen
  }
}

// What each level holds of the looks and of room events, for the choices to
// show what is in effect: every chat's; the chosen chat's own, and its
// account's.
void app::show_levels() {
  {
    shared.looks.at(mux::choice_level::everywhere{}) = {this->appearance().wallpaper, this->appearance().bubbles, this->appearance().panels};
    mux::ui::looks_held account_held, chat_held;
    if (const auto& chosen = root().main().chosen) {
      chat_held.wallpaper = this->own_of<&mux::app::chat_choices::wallpaper>(*chosen);
      chat_held.bubbles = this->own_of<&mux::app::chat_choices::bubbles>(*chosen);
      chat_held.panels = this->own_of<&mux::app::chat_choices::panels>(*chosen);
      if (const auto* account = this->settings_of(chosen->account.address)) {
        if (const auto& word = mux::config::wallpaper_of(*account))
          account_held.wallpaper = mux::config::wallpaper_of(std::string_view(*word));
        if (const auto& word = mux::config::bubbles_of(*account))
          account_held.bubbles = mux::config::bubble_look_of(*word);
        if (const auto& word = mux::config::panels_of(*account))
          account_held.panels = mux::config::bubble_look_of(*word);
      }
    }
    shared.looks.at(mux::choice_level::account{}) = std::move(account_held);
    shared.looks.at(mux::choice_level::chat{}) = std::move(chat_held);
  }
  // And of room events, for their lists to show what is in effect.
  {
    mux::ui::room_events_at(mux::choice_level::everywhere{}) = {this->history().show_room_events, this->history().room_event_kinds};
    mux::ui::room_events_held account_held, chat_held;
    if (const auto& chosen = root().main().chosen) {
      chat_held.all = this->own_of<&mux::app::chat_choices::room_events>(*chosen);
      chat_held.kinds = this->own_of<&mux::app::chat_choices::room_event_kinds>(*chosen);
      if (const auto* account = this->settings_of(chosen->account.address)) {
        account_held.all = mux::config::room_events_of(*account);
        account_held.kinds = mux::config::room_event_kinds_of(*account);
      }
    }
    mux::ui::room_events_at(mux::choice_level::account{}) = std::move(account_held);
    mux::ui::room_events_at(mux::choice_level::chat{}) = std::move(chat_held);
  }
}

void app::show_backgrounds() {
  // Behind the whole window, where it is so: the chat's, else every chat's.
  const auto& chosen = root().main().chosen;
  root().show_behind(chosen ? this->wallpaper_of(*chosen)
                            : this->appearance().wallpaper.value_or(mux::config::wallpaper_t{mux::config::wallpaper::theme{}}));
}

}  // namespace mux::app
