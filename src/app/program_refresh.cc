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

// The chats muted, and those listed in other accounts' lists with their
// strips: each its own colour, else its account's; shown as it says, else
// as its account.
void app::show_placements() {
  root().main().muted = this->chats_where<&mux::app::chat_choices::muted>();
  {
    auto& screen = root().main();
    screen.listed_in.clear();
    screen.moved_out.clear();
    screen.strips.clear();
    for (const mux::config::chat_placement& one : placements) {
      const mux::conversation_id chat{{mux::ui::protocol_of(one.account), one.account}, one.conversation};
      const mux::account_id to{mux::ui::protocol_of(one.listed_in), one.listed_in};
      screen.listed_in[to].push_back(chat);
      if (one.moved)
        screen.moved_out.insert(chat);
      const auto* own = this->settings_of(one.account);
      const bool on = one.strip.value_or(own == nullptr || mux::config::strip_of(*own));
      if (!on)
        continue;
      const mux::config::accent_t colour = one.strip_colour ? mux::config::accent_of(one.strip_colour)
                                           : own != nullptr ? mux::config::colour_of(*own)
                                                                : mux::config::default_colour_of(one.account);
      screen.strips.insert_or_assign(chat, mux::ui::colour_of(colour, this->appearance().theme));
    }
    screen.side.current_account = screen.current;
    screen.side.theme_now = this->appearance().theme;
    screen.side.accounts_known.clear();
    for (const auto& [id, account] : model->accounts())
      screen.side.accounts_known.push_back(id);
  }
}

// Which chats show what is done in them, as the settings say now. In an
// encrypted room, who joins and who is invited is always shown: each of
// them is given the room's key, and the server could put anyone there --
// the one thing to see before writing on.
void app::show_event_filters() {
  root().main().event_filters =
      std::ranges::to<std::remove_cvref_t<decltype(root().main().event_filters)>>(std::views::transform(this->all_chats(), [this](const mux::conversation& one) {
        auto filter = this->room_event_filter_of(one.id);
        if (one.encrypted)
          for (const mux::room_event_t kind : {mux::room_event_t{mux::room_event::joins{}}, mux::room_event_t{mux::room_event::invites{}}})
            filter.shown[kind.index()] = true;
        return std::pair{one.id, filter};
      }));
}

// The chosen chat's bubbles and the panels' look, as its levels say.
void app::show_looks_now() {
  root().main().bubbles = root().main().chosen ? this->bubbles_of(*root().main().chosen) : mux::config::bubble_look{};
  // The panels' look, as the chosen chat's levels say, else every chat's:
  // the whole window repainted where it changes -- nothing made again.
  shared.looks.panels = root().main().chosen ? this->panels_of(*root().main().chosen)
                                                   : this->appearance().panels.value_or(mux::config::bubble_look{});
  if (mux::ui::show_panels(shared.paint, shared.looks.panels, shared.looks.window, colours)) {
    root().markDamaged();
    skiff::scene::work::mark(root().main().fState.fId);  // an ease ticked by the screen
  }
}

void app::show_space_bars() {
  root().main().spaces_on = this->appearance().spaces;
  root().main().top_bar_on = this->appearance().top_bar;
  root().main().space_places = this->appearance().space_places;
  root().main().forums = this->chats_where<&mux::app::chat_choices::forum>();
  root().main().hidden_from_home = this->chats_where<&mux::app::chat_choices::hidden_from_home>();
  // Home without what spaces hold: the account's own choice, else every one's.
  root().main().home_hides_spaced = [&] {
    if (const auto& by = root().main().current)
      if (const auto* account = this->settings_of(by->address))
        if (const auto& own = mux::config::home_hides_of(*account))
          return *own;
    return this->appearance().home_hides_spaced;
  }();
  root().main().home_hides_direct = [&] {
    if (const auto& by = root().main().current)
      if (const auto* account = this->settings_of(by->address))
        if (const auto& own = mux::config::home_direct_of(*account))
          return *own;
    return this->appearance().home_hides_direct;
  }();
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
  root().main().wallpaper = root().main().chosen ? this->wallpaper_of(*root().main().chosen)
                                                 : mux::config::wallpaper_t{mux::config::wallpaper::theme{}};
  // Behind the whole window, where it is so: the chat's, else every chat's.
  root().show_behind(root().main().chosen ? root().main().wallpaper
                                          : this->appearance().wallpaper.value_or(mux::config::wallpaper_t{mux::config::wallpaper::theme{}}));
}

// Which chats show who has read up to where, which show no link previews,
// and how far a jump's search pages back in each.
void app::show_chat_choices() {
  auto& screen = root().main();
  screen.receipts_in = std::ranges::to<std::remove_cvref_t<decltype(screen.receipts_in)>>(std::views::transform(std::views::filter(this->all_chats(), [this](const mux::conversation& one) { return this->receipts_shown(one.id); }), &mux::conversation::id));
  screen.previews_off = std::ranges::to<std::remove_cvref_t<decltype(screen.previews_off)>>(std::views::transform(std::views::filter(this->all_chats(), [this](const mux::conversation& one) { return !this->previews_shown(one.id); }), &mux::conversation::id));
  screen.jump_limits = std::ranges::to<std::remove_cvref_t<decltype(screen.jump_limits)>>(std::views::transform(this->all_chats(), [this](const mux::conversation& one) {
                         return std::pair{one.id, this->jump_search_of(one.id)};
                       }));
}

}  // namespace mux::app
