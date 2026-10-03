// SPDX-License-Identifier: AGPL-3.0-only
// The program: waking on changes, keeping what changed, frames, drafts, the screens shown.
module mux.app.program;

import mux.vault;
import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.platform.audio;
import mux.platform.notifications;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import skiff.paint;
import skiff.scene;
import skiff.nodes.text;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.logic.links;

namespace mux::app {

auto app::window() -> skiff::scene::Scene<window_type>& { return scene; }

void app::woken() {
  // What UnifiedPush's connector said, first: a push is a sync now.
  notices.take_push();
  // What the workers made, put where it goes: on this, the UI's thread.
  work.finish();
  auto changes = box->take();
  if (changes.empty())
    return;
  for (const auto& one : changes) {
    // What the program itself does with a change, besides the model: a
    // session kept, a picture shown.
    splice::visit(splice::overloaded{[&](const mux::change::avatar_loaded& picture) {
                                 pictures.take(picture, true);
                                 mux::ui::download_progress().erase(picture.source);
                               },
                               // A room the user made: shown, once the model has it.
                               [&](const mux::change::room_created& made) { made_room_ = made.id; },
                               // A directory searched: its rooms, in Explore.
                               [&](const mux::change::directory_listed& listed) {
                                 root().show_directory(listed.rooms, listed.server, listed.space);
                                 // The own server's, for what the chat list searched.
                                 if (listed.server.empty() && !listed.space)
                                   root().main().found_rooms_elsewhere(listed.query, listed.rooms);
                               },
                               // Something the server refused: a notice saying why.
                               [&](const mux::change::refused& said) { root().show_message("Not done", said.what); },
                               [&](const mux::change::notice& said) { root().show_message(said.heading, said.what); },
                               // People found: in Start chat, while it asks for them.
                               // A person's profile: their picture asked for, where they have one.
                               [&](const mux::change::profile_found& found) {
                                 pictures.profile_avatars.insert_or_assign(found.user, found.avatar);
                                 pictures.ask();
                               },
                               // An invite come: said once a run.
                               [&](const mux::change::conversation_updated& updated) {
                                 if (updated.invite)
                                   notices.invite_came(updated.id, *updated.invite, updated.name);
                               },
                               [&](const mux::change::devices_listed& listed) {
                                 if (person_open_ && person_open_->first.account == listed.by && person_open_->second == listed.user) {
                                   const auto [chat, user] = *person_open_;
                                   root().open_person(chat.account, user,
                                                      mux::ui::person_of(model->find(chat), *model, chat.account, user));
                                 }
                               },
                               [&](const mux::change::trust_changed& told) {
                                 if (person_open_ && person_open_->first.account == told.by && person_open_->second == told.user) {
                                   const auto [chat, user] = *person_open_;
                                   root().open_person(chat.account, user,
                                                      mux::ui::person_of(model->find(chat), *model, chat.account, user));
                                 }
                               },
                               [&](const mux::change::people_found& found) {
                                 root().show_found_people(found.people, found.query);
                                 root().main().found_people_elsewhere(found.query, found.people);
                               },
                               // A room looked up: its card filled, while it
                               // is up for that room still.
                               [&](const mux::change::room_previewed& shown) {
                                 // Asked for a pill: the room there or not, and
                                 // its picture, under the address it was named by.
                                 if (pictures.pill_rooms.contains(shown.asked) &&
                                     !(previewing && previewing->step.room == shown.asked)) {
                                   if (!shown.preview.id.empty()) {
                                     mux::ui::rooms_found().insert_or_assign(shown.asked, shown.preview.name);
                                     if (shown.preview.avatar)
                                       net->fetch_avatar(shown.by, *shown.preview.avatar, shown.asked);
                                   }
                                   return;
                                 }
                                 if (!previewing || !root().room_card_up() || previewing->step.by != shown.by ||
                                     previewing->step.room != shown.asked)
                                   return;
                                 // A room joined after all -- by an address not
                                 // known here: opened, not offered to join.
                                 for (const auto& [id, account] : model->accounts())
                                   for (const auto& [key, chat] : account.conversations)
                                     if (!shown.preview.id.empty() && chat.id.id == shown.preview.id) {
                                       root().close_room_card();
                                       this->open_chat(chat.id, std::nullopt);
                                       return;
                                     }
                                 if (shown.preview.avatar && !shown.preview.id.empty())
                                   net->fetch_avatar(shown.by, *shown.preview.avatar, shown.preview.id);
                                 root().open_room_card(shown.asked, shown.preview);
                               },
                               [&](const mux::change::media_progress& how) {
                                 mux::ui::download_progress().insert_or_assign(how.source, how.done);
                                 scene.state().markDamaged();
                               },
                               // What an account's protocol is now: what is offered of it.
                               [&](const mux::change::protocol_state_changed& now) {
                                 mux::ui::protocol_states().insert_or_assign(now.account, now.now);
                                 this->refresh();
                               },
                               // A protocol's own: what the program does with it, as the
                               // protocol says (mux.app.proto); nothing by default.
                               [&](const auto& theirs) {
                                 using mux::app::defaults::program_told;
                                 program_told(*this, theirs);
                               }},
               one);
    // A message deleted: marked where it is kept, and kept whole apart --
    // as it was, before the model takes it out of view.
    if (!ask.demo)
      splice::visit(splice::overloaded{[&](const mux::change::message_redacted& c) {
                                   std::optional<mux::message> was;
                                   if (const mux::conversation* chat = model->find(c.in))
                                     if (const auto found = std::ranges::find(chat->timeline, c.id, &mux::message::id);
                                         found != chat->timeline.end())
                                       was = *found;
                                   store.mark_deleted(c.in, c.id, was);
                                 },
                                 [](const auto&) {}},
                 one);
    model->apply(one);
    paging.keep(one);
  }
  // The marks: those read back put in, written where they changed, and a
  // mark made kept with its message.
  marks.took(changes);
  // What came as it happened, notified: mentions known by the marks the same
  // changes made.
  if (!ask.demo) {
    std::set<std::string> mentioning;
    for (const mux::change_t& one : changes)
      splice::visit(splice::overloaded{[&](const mux::change::mentioned& m) { mentioning.insert(m.event); },
                                 [](const auto&) {}},
                 one);
    for (const mux::change_t& one : changes)
      splice::visit(splice::overloaded{[&](const mux::change::message_added& added) {
                                   const bool live = splice::visit(
                                       splice::overloaded{[](mux::placement::at_end) { return true; },
                                                       [](const auto&) { return false; }},
                                       added.where);
                                   if (live)
                                     notices.message_came(added.message, mentioning.contains(added.message.id),
                                                          on_screen && root().main().chosen == added.message.in);
                                 },
                                 [](const auto&) {}},
                 one);
  }
  // Messages held to a number in all, least recently read out first.
  model->trim(static_cast<std::size_t>(limits.messages_in_memory), root().main().chosen);
  this->refresh();
  // A room the user made: opened once the model has it.
  if (made_room_ && model->find(*made_room_))
    this->open_chat(*std::exchange(made_room_, std::nullopt), std::nullopt);
  // A room joined from a link: opened once it is here.
  if (joining)
    if (const auto found = mux::logic::chat_of(*model, *joining)) {
      const auto link = *std::exchange(joining, std::nullopt);
      // A message in it, where the link is to one.
      const auto event = splice::visit(
          splice::overloaded{[](const mux::logic::mention::place& one) { return one.event; },
                             [](const auto&) { return std::optional<std::string>(); }},
          mux::logic::mention_in(link));
      if (event)
        this->go_to_linked(*found, *event);
      else
        this->open_chat(*found, std::nullopt);
    }
  // What was fetched for the marks: kept with them, the list shown again.
  marks.fetched(changes);
  // A message jumped to that the server says is not there: the jump
  // stopped, and said why -- it paged the whole history back for it.
  for (const mux::change_t& one : changes)
    splice::visit(splice::overloaded{[&](const mux::change::event_missing& gone) {
                                       auto& screen = root().main();
                                       if (screen.jumping_to == gone.id) {
                                         // An edit: to the message it edits, which is where it shows.
                                         if (gone.instead) {
                                           screen.jump_to(*gone.instead);
                                         } else {
                                           screen.stop_jump();
                                           root().show_notice("That message isn't there any more, or can't be seen from this account.");
                                         }
                                       }
                                     },
                                     [](const auto&) {}},
                  one);
  // A link's message fetched: in its thread, where it is in one.
  if (linked_)
    if (const mux::conversation* chat = model->find(linked_->first); chat && mux::ui::held_message(*chat, linked_->second)) {
      const auto [in, event] = *std::exchange(linked_, std::nullopt);
      if (root().main().chosen == in)
        (void)this->open_in_thread(*chat, event);
    }
  // An answer gone to, scrolled to once its thread's panel has it.
  if (thread_target_) {
    auto& screen = root().main();
    if (!screen.thread_open() || screen.parts.threads.scroll_to(*thread_target_))
      thread_target_.reset();
  }
}









void app::wire() {
  shared = services{.model = model,
                    .net = net,
                    .store = &store,
                    .box = box,
                    .ask = &ask,
                    .scene = &scene,
                    .kept = this,
                    .vault = vault,
                    .work = &work,
                    .system_dialogs = &system_dialogs,
                    .wake = &wake,
                    .speaker = &speaker};
  store.vault = vault;
}

void app::before_frame() {
  ++mux::ui::image_cache::frame();
  root().drop_closed();
  // What has been on screen in the chat shown is read, as far as it goes,
  // as in tdesktop: the chat list's counts go down as it is read, not all
  // at once when it is opened or left.
  if (const auto& chosen = root().main().chosen; chosen && !root().open_panel() && !root().settings_up())
    if (const auto seen = root().main().last_seen(); seen && reading.mark_seen(*chosen, *seen))
      this->refresh();
  // The mentions and reactions for the user whose messages are on screen,
  // seen -- only those, as tdesktop's.
  if (const auto& chosen = root().main().chosen)
    if (const mux::conversation* chat = model->find(*chosen);
        chat && (!chat->unread_mentions.empty() || !chat->unread_reactions.empty()) && !root().open_panel()) {
      auto shown = root().main().shown_now();
      const auto marked = [&](const mux::unread_mark& mark) { return std::ranges::contains(shown, mark.target); };
      if (std::ranges::any_of(chat->unread_mentions, marked) || std::ranges::any_of(chat->unread_reactions, marked)) {
        model->apply(mux::change_t{mux::change::marks_shown{*chosen, std::move(shown)}});
        marks.save();
        this->refresh();
      }
    }
  // The input has the keyboard's focus whenever nothing else does -- at the
  // start, after a panel or a search closes -- so typing always goes to it,
  // as in tdesktop.
  // Not under a finger: there the keys' focus is the text input started,
  // and a phone's keyboard up over the chat until it is put away.
  if (scene.focusedId() == 0 && !by_touch && root().main().chosen && !root().open_panel() && !root().settings_up())
    scene.focus(root().main().line.field);
  // The emoji panel's stickers in view, asked for as they change -- as it
  // opens, as it scrolls, as its tab or its search changes -- and not only
  // when something else refreshed the window.
  if (auto shown = root().emoji_pictures_shown(); shown != mux::ui::panel_pictures_shown()) {
    mux::ui::panel_pictures_shown() = std::move(shown);
    pictures.ask();
  }
  auto pending = std::exchange(ask.requests, {});
  for (const request_t& one : pending)
    splice::visit([this](const auto& each) { this->route(each); }, one);
  // A selectable text pressed with the right button: its menu, the last
  // asked for.
  if (auto asked = std::exchange(skiff::nodes::textMenusAsked(), {}); !asked.empty() && !root().context_menu_up())
    root().show_text_menu(std::move(asked.back().text), std::move(asked.back().link));
  // What the parts left to do: the window made again, brought up to date;
  // the emoji picked lately kept.
  if (std::exchange(shared.rebuild_due, false))
    this->rebuild_in_theme();
  if (std::exchange(shared.refresh_due, false))
    this->refresh();
  if (std::exchange(mux::ui::recent_emoji_changed(), false)) {
    recent_emoji = mux::ui::recent_emoji();
    (void)this->write();
  }
  if (std::exchange(mux::ui::stickers_changed(), false)) {
    recent_stickers = mux::ui::recent_stickers();
    favourite_stickers = mux::ui::favourite_stickers();
    (void)this->write();
  }
  if (drawer_waits && !root().pages_moving()) {
    root().close_drawer_now();
    drawer_waits = false;
  }
}

void app::closing() {
  if (const auto& chosen = root().main().chosen)
    drafts.keep(*chosen, root().main().line.plain());
  net->shutdown();
}

auto app::root() -> window_type& { return scene.root(); }

void app::show_conversations() {
  root().close_drawer();
  pending_login.reset();
  root().close();
  this->refresh();
}

auto app::show_accounts() -> accounts& {
  // From the drawer, the page comes in over it, and the drawer goes once
  // the page is in: not two things moving at once.
  if (root().drawer_open())
    drawer_waits = true;
  root().close_settings();
  pending_login.reset();
  auto& panel = root().open<accounts>();
  if (config_error)
    panel.say(*config_error);
  this->refresh();
  return panel;
}

auto app::show_account(const std::string& address) -> accounts& {
  auto& panel = this->show_accounts();
  if (const auto found = this->find(address); found != saved.end()) {
    panel.select(*found, *model);
    panel.show(saved, *model);
  }
  return panel;
}

void app::show_adding() {
  auto& panel = this->show_accounts();
  panel.proxies = proxies;
  panel.show_adding();
  this->refresh();
}

auto app::form_up() -> mux::ui::account_form<actions>* {
  auto* up = root().open_panel();
  if (!up)
    return nullptr;
  return splice::visit([](auto& panel) { return panel.form(); }, *up);
}

void app::refresh(std::source_location from) {
  static const bool traced = std::getenv("MUX_TRACE_FRAMES") != nullptr;
  if (traced)
    std::println(std::cerr, "[frame] refresh from {}:{} ({})", std::filesystem::path(from.file_name()).filename().string(),
                 from.line(), from.function_name());
  // Off screen: done once, as the window comes back.
  if (!on_screen) {
    refresh_waiting_ = true;
    return;
  }
  pictures.ask();
  // The space each chat is in: its choices, where the chat has none.
  space_above.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations)
      if (one.space)
        for (const std::string& child : one.children)
          if (child != one.id.id)
            space_above.try_emplace(mux::conversation_id{one.id.account, child}, one.id);
  root().main().muted = muted;
  // The chats listed in other accounts' lists, and their strips: each its
  // own colour, else its account's; shown as it says, else as its account.
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
      const auto own = this->find(one.account);
      const bool on = one.strip.value_or(own == saved.end() || mux::config::strip_of(*own));
      if (!on)
        continue;
      const mux::config::accent_t colour = one.strip_colour ? mux::config::accent_of(one.strip_colour)
                                           : own != saved.end() ? mux::config::colour_of(*own)
                                                                : mux::config::default_colour_of(one.account);
      screen.strips.insert_or_assign(chat, mux::ui::colour_of(colour, theme));
    }
    screen.side.current_account = screen.current;
    screen.side.theme_now = theme;
    screen.side.accounts_known.clear();
    for (const auto& [id, account] : model->accounts())
      screen.side.accounts_known.push_back(id);
  }
  // Which chats show what is done in them, as the settings say now.
  auto& filters = root().main().event_filters;
  filters.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations) {
      auto filter = this->room_event_filter_of(one.id);
      // In an encrypted room, who joins and who is invited is always shown:
      // each of them is given the room's key, and the server could put
      // anyone there -- the one thing to see before writing on.
      if (one.encrypted)
        for (const mux::room_event_t kind : {mux::room_event_t{mux::room_event::joins{}}, mux::room_event_t{mux::room_event::invites{}}})
          filter.shown[kind.index()] = true;
      filters.emplace(one.id, filter);
    }
  // The chosen chat's bubbles, as its levels say.
  root().main().bubbles = root().main().chosen ? this->bubbles_of(*root().main().chosen) : mux::config::bubble_look{};
  // The panels' look, as the chosen chat's levels say, else every chat's:
  // the whole window repainted where it changes -- nothing made again.
  mux::ui::panel_look_now() = root().main().chosen ? this->panels_of(*root().main().chosen)
                                                   : panels.value_or(mux::config::bubble_look{});
  if (mux::ui::show_panels(mux::ui::panel_look_now())) {
    root().markDamaged();
    skiff::scene::work::mark(root().main().fState.fId);  // an ease ticked by the screen
  }
  // The space bars, as the settings put them.
  root().main().spaces_on = spaces;
  root().main().top_bar_on = top_bar;
  root().main().space_places = space_places;
  root().main().forums = forums;
  root().main().hidden_from_home = hidden_from_home;
  // Home without what spaces hold: the account's own choice, else every one's.
  root().main().home_hides_spaced = [&] {
    if (const auto& by = root().main().current)
      if (const auto* account = this->settings_of(by->address))
        if (const auto& own = mux::config::home_hides_of(*account))
          return *own;
    return home_hides_spaced;
  }();
  root().main().home_hides_direct = [&] {
    if (const auto& by = root().main().current)
      if (const auto* account = this->settings_of(by->address))
        if (const auto& own = mux::config::home_direct_of(*account))
          return *own;
    return home_hides_direct;
  }();
  // What each level holds of the looks, for the choices to show: every
  // chat's; the chosen chat's own, and its account's.
  {
    mux::ui::looks_at(mux::choice_level::everywhere{}) = {wallpaper, bubbles, panels};
    mux::ui::looks_held account_held, chat_held;
    if (const auto& chosen = root().main().chosen) {
      if (const auto own = wallpaper_in.find(*chosen); own != wallpaper_in.end())
        chat_held.wallpaper = own->second;
      if (const auto own = bubbles_in.find(*chosen); own != bubbles_in.end())
        chat_held.bubbles = own->second;
      if (const auto own = panels_in.find(*chosen); own != panels_in.end())
        chat_held.panels = own->second;
      if (const auto* account = this->settings_of(chosen->account.address)) {
        if (const auto& word = mux::config::wallpaper_of(*account))
          account_held.wallpaper = mux::config::wallpaper_of(std::string_view(*word));
        if (const auto& word = mux::config::bubbles_of(*account))
          account_held.bubbles = mux::config::bubble_look_of(*word);
        if (const auto& word = mux::config::panels_of(*account))
          account_held.panels = mux::config::bubble_look_of(*word);
      }
    }
    mux::ui::looks_at(mux::choice_level::account{}) = std::move(account_held);
    mux::ui::looks_at(mux::choice_level::chat{}) = std::move(chat_held);
  }
  // And of room events, for their lists to show what is in effect.
  {
    mux::ui::room_events_at(mux::choice_level::everywhere{}) = {history.show_room_events, history.room_event_kinds};
    mux::ui::room_events_held account_held, chat_held;
    if (const auto& chosen = root().main().chosen) {
      if (const auto own = room_events.find(*chosen); own != room_events.end())
        chat_held.all = own->second;
      if (const auto own = room_event_kinds.find(*chosen); own != room_event_kinds.end())
        chat_held.kinds = own->second;
      if (const auto* account = this->settings_of(chosen->account.address)) {
        account_held.all = mux::config::room_events_of(*account);
        account_held.kinds = mux::config::room_event_kinds_of(*account);
      }
    }
    mux::ui::room_events_at(mux::choice_level::account{}) = std::move(account_held);
    mux::ui::room_events_at(mux::choice_level::chat{}) = std::move(chat_held);
  }
  // The chosen chat's background, as its levels say.
  root().main().wallpaper = root().main().chosen ? this->wallpaper_of(*root().main().chosen)
                                                 : mux::config::wallpaper_t{mux::config::wallpaper::theme{}};
  // Behind the whole window, where it is so: the chat's, else every chat's.
  root().show_behind(root().main().chosen ? root().main().wallpaper
                                          : wallpaper.value_or(mux::config::wallpaper_t{mux::config::wallpaper::theme{}}));
  // And which show who has read up to where.
  auto& receipts = root().main().receipts_in;
  receipts.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations)
      if (this->receipts_shown(one.id))
        receipts.insert(one.id);
  // And which show no link previews.
  auto& unpreviewed = root().main().previews_off;
  unpreviewed.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations)
      if (!this->previews_shown(one.id))
        unpreviewed.insert(one.id);
  // And how far a jump's search pages back in each.
  auto& limits = root().main().jump_limits;
  limits.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations)
      limits.emplace(one.id, this->jump_search_of(one.id));
  root().show(saved, *model);
  root().main().show(*model);
  if (auto* up = root().open_panel())
    splice::visit([this](auto& panel) { this->bring_up_to_date(panel); }, *up);
}

void app::bring_up_to_date(accounts& panel) {
  panel.proxies = proxies;
  panel.show(saved, *model);
  if (auto* pane = panel.adding()) {
    pane->set_proxies(proxies);
    splice::visit([this](auto& form) { this->watch_login(form); }, pane->parts.form);
  }
}

// What is kept, applied: once at the start, or once the vault is opened.
void app::begin(const mux::config::file& saved, std::vector<mux::config::account_t> extra, bool demo,
                std::optional<std::string> error) {
  const auto proxies = saved.proxies.value_or(std::vector<mux::config::proxy_settings>{});
  for (const auto& one : mux::config::accounts_of(saved))
    if (mux::config::enabled_of(one) && !demo)
      net->start(one, proxies);
  for (const auto& one : extra)
    net->start(one, proxies);
  net->thread = std::thread([n = net] {
    try {
      n->loop.run_forever();
    } catch (const std::exception& failed) {
      std::println(std::cerr, "[mux] the network stopped: {}", failed.what());
    }
  });

  this->keeps_nothing = demo;
  // What each protocol's account does, for the window to offer.
  mux::app::tell_protocol_ops(mux::protocols{});
  this->saved = mux::config::accounts_of(saved);
  this->foreign_accounts = mux::config::foreign_of(saved);
  this->motion = saved.motion;
  // The account shown last, shown again once it is in the model: accounts
  // arrive after the first frame, and the first one there is not the one.
  this->last_account = saved.last_account;
  // The emoji picked lately: shown first in the panels, and kept as picked.
  this->recent_emoji = saved.recent_emoji.value_or(std::vector<std::string>{});
  mux::ui::recent_emoji() = this->recent_emoji;
  // And the stickers sent lately, and the favourites.
  const auto emotes_of = [](const std::optional<std::vector<mux::config::sticker_kept>>& kept) {
    return kept.value_or(std::vector<mux::config::sticker_kept>{}) | std::views::transform([](const mux::config::sticker_kept& one) {
             return mux::emote{.shortcode = one.shortcode, .url = one.url, .body = one.body, .w = one.w, .h = one.h, .size = one.size,
                               .mimetype = one.mimetype};
           }) |
           std::ranges::to<std::vector>();
  };
  this->recent_stickers = emotes_of(saved.recent_stickers);
  this->favourite_stickers = emotes_of(saved.favourite_stickers);
  mux::ui::recent_stickers() = this->recent_stickers;
  mux::ui::favourite_stickers() = this->favourite_stickers;
  if (saved.last_account)
    this->root().main().wanted = mux::account_id{mux::ui::protocol_of(*saved.last_account), *saved.last_account};
  this->theme = mux::config::theme_of(saved.theme);
  if (saved.wallpaper)
    this->wallpaper = mux::config::wallpaper_of(std::string_view(*saved.wallpaper));
  if (saved.bubbles)
    this->bubbles = mux::config::bubble_look_of(*saved.bubbles);
  mux::ui::bubble_look_everywhere() = this->bubbles.value_or(mux::config::bubble_look{});
  if (saved.panels)
    this->panels = mux::config::bubble_look_of(*saved.panels);
  mux::ui::panel_look_everywhere() = this->panels.value_or(mux::config::bubble_look{});
  this->accent = mux::config::accent_of(saved.accent);
  this->renderer = mux::config::renderer_of(saved.renderer);
  this->partial_redraw = saved.partial_redraw.value_or(false);
  this->flash_redraws = saved.flash_redraws.value_or(false);
  this->vsync = saved.vsync.value_or(true);
  this->window_opacity = std::clamp(saved.window_opacity.value_or(100), 20, 100);
  this->wallpaper_behind = mux::ui::window_look().behind;
  mux::ui::window_look().live_blur = saved.live_blur.value_or(false);
  this->live_blur = mux::ui::window_look().live_blur;
  this->frost_blur = mux::ui::window_look().frost;
  this->spaces = saved.spaces.value_or(true);
  this->top_bar = saved.top_bar.value_or(true);
  this->home_hides_spaced = saved.home_hides_spaced.value_or(false);
  mux::ui::window_look().home_hides = this->home_hides_spaced;
  this->home_hides_direct = saved.home_hides_direct.value_or(false);
  mux::ui::window_look().home_direct = this->home_hides_direct;
  mux::ui::window_look().spaces = this->spaces;
  mux::ui::window_look().top_bar = this->top_bar;
  if (saved.space_places)
    this->space_places = *saved.space_places | std::views::transform([](const mux::config::space_place& one) {
                             return mux::config::space_placed{one.account, mux::config::space_item_of(one.item),
                                                              mux::config::space_bar_of(one.bar)};
                           }) |
                           std::ranges::to<std::vector>();
  this->show_fps = saved.show_fps.value_or(false);
  this->interface_scale = std::clamp(saved.interface_scale.value_or(100), mux::ui::kScaleLeast, mux::ui::kScaleMost);
  mux::ui::window_look().interface_scale = this->interface_scale;
  this->limits = saved.cache.value_or(mux::config::cache_limits{});
  if (!demo)
    this->drafts.load();
  this->sending = saved.sending.value_or(mux::config::sending_settings{});
  this->history = saved.history.value_or(mux::config::history_settings{});
  this->model->show_deleted = this->history.show_deleted;
  this->settings.apply_limits();
  this->proxies = proxies;
  marks.load();
  this->notifications = saved.notifications.value_or(mux::config::notification_settings{});
  // UnifiedPush only where chosen; off by default.
  if (notifications.unified_push.value_or(false) && !demo)
    notices.start_push();
  for (const auto& one : saved.chat_notify.value_or(std::vector<mux::config::chat_notify>{}))
    this->notify_modes.insert_or_assign(
        mux::conversation_id{{mux::ui::protocol_of(one.account), one.account}, one.conversation},
        mux::config::notify_mode_of(one.mode));
  for (const auto& one : saved.room_events.value_or(std::vector<mux::config::room_events_choice>{})) {
    const mux::conversation_id chat{{mux::ui::protocol_of(one.account), one.account}, one.conversation};
    if (one.show)
      this->room_events.insert_or_assign(chat, *one.show);
    if (one.kinds)
      this->room_event_kinds.insert_or_assign(chat, *one.kinds);
    if (one.receipts)
      this->receipts_shown_in.insert_or_assign(chat, *one.receipts);
    if (one.previews)
      this->previews_shown_in.insert_or_assign(chat, *one.previews);
    if (one.typing)
      this->typing_sent_in.insert_or_assign(chat, *one.typing);
    if (one.previews_direct)
      this->previews_direct_in.insert_or_assign(chat, *one.previews_direct);
    if (one.jump_search)
      this->jump_search_in.insert_or_assign(chat, *one.jump_search);
    if (one.wallpaper)
      this->wallpaper_in.insert_or_assign(chat, mux::config::wallpaper_of(std::string_view(*one.wallpaper)));
    if (one.bubbles)
      this->bubbles_in.insert_or_assign(chat, mux::config::bubble_look_of(*one.bubbles));
    if (one.panels)
      this->panels_in.insert_or_assign(chat, mux::config::bubble_look_of(*one.panels));
    if (one.forum.value_or(false))
      this->forums.insert(chat);
    if (one.hide_from_home.value_or(false))
      this->hidden_from_home.insert(chat);
  }
  this->placements = saved.placements.value_or(std::vector<mux::config::chat_placement>{});
  for (const auto& one : saved.muted.value_or(std::vector<mux::config::muted_chat>{}))
    this->muted.insert({{mux::ui::protocol_of(one.account), one.account}, one.conversation});
  skiff::paint::motionLevel() = motion_of(saved.motion);
  this->root().show_motion(saved.motion.value_or("full"));
  this->config_error = std::move(error);
  this->refresh();
}

void app::lock(std::vector<mux::config::account_t> extra, bool demo) {
  waiting_extra = std::move(extra);
  waiting_demo = demo;
  root().ask_passphrase(mux::config::passphrase_for::unlock{});
}

// The files sealed: the settings, and every file in the state directory
// that is read through the vault -- a chat's messages and its deleted ones
// a line at a time, the rest whole. Not the wallpapers, read as pictures.
mux::vault::vault::kept_files app::sealed_files() const {
  mux::vault::vault::kept_files out;
  out.whole.push_back(config_path);
  const auto root = mux::config::state_path("").parent_path();
  std::error_code failed;
  if (!std::filesystem::exists(root, failed))
    return out;
  for (auto walk = std::filesystem::recursive_directory_iterator(root, failed);
       !failed && walk != std::filesystem::recursive_directory_iterator(); walk.increment(failed)) {
    const auto& path = walk->path();
    if (walk->is_directory() && path.filename() == "wallpapers") {
      walk.disable_recursion_pending();
      continue;
    }
    if (!walk->is_regular_file())
      continue;
    const auto name = path.filename().string();
    if (name.ends_with(".new") || name.ends_with(".unreadable"))
      continue;
    (path.extension() == ".jsonl" ? out.lines : out.whole).push_back(path);
  }
  return out;
}

// Read first, as the vault is; then its journal begun (turn), everything
// written again, and the journal ended. Cut short after the turn -- an
// error, a crash -- the journal stays: every file is still opened, by
// either key or plain, and the re-seal is finished at the next start.
template <class Turn>
bool app::reseal(Turn turn) {
  auto& vault = *this->vault;
  // All of it with every other read and write of the vault waiting: the
  // network's saves and the store's lines come after, under the new key.
  return vault.exclusive([&] {
    const auto all = vault.read_all(this->sealed_files());
    if (!all)
      return false;
    turn(vault);
    if (!vault.write_all(*all))
      return false;
    vault.finish();
    return true;
  });
}

// Why a re-seal did not happen, or was not all done.
inline constexpr std::string_view kUnread = "Something kept could not be read, so nothing was changed.";
inline constexpr std::string_view kCutShort =
    "Not everything could be written. Everything stays readable, and it is finished at the next start.";

// A passphrase given, by what it was asked for.
void app::apply(const request::give_passphrase& one) {
  auto& vault = *this->vault;
  // A new one: not empty, and the same twice.
  const auto fresh_refused = [&] { return mux::config::new_passphrase_refused(one.fresh, one.again); };
  const auto done = [&] {
    root().close_passphrase();
    if (auto* up = root().settings_up())
      if (auto* page = up->storage())
        page->show_sealed(vault.on());
  };
  splice::visit(
      splice::overloaded{
          [&](mux::config::passphrase_for::unlock) {
            if (!vault.unlock(one.current))
              return root().passphrase_refused("That is not the passphrase.");
            // A re-seal cut short last time: finished first.
            if (vault.resealing() && !this->reseal([](mux::vault::vault&) {}))
              root().show_message("Local data", "Re-sealing what is kept, begun before, could not be finished. It is "
                                                "tried again at the next start; everything stays readable.");
            root().close_passphrase();
            mux::config::file saved;
            std::optional<std::string> error;
            if (auto loaded = mux::config::load(config_path, vault))
              saved = std::move(*loaded);
            else
              error = loaded.error();
            this->begin(saved, std::move(waiting_extra), waiting_demo, std::move(error));
          },
          [&](mux::config::passphrase_for::encrypt) {
            if (vault.on())
              return done();
            if (auto refused = fresh_refused())
              return root().passphrase_refused(*refused);
            if (!this->reseal([&](mux::vault::vault& v) { v.begin_encrypt(one.fresh); }))
              return root().passphrase_refused(std::string(vault.resealing() ? kCutShort : kUnread));
            // Pictures are not kept on disk while it is on: those there go.
            std::error_code ignored;
            std::filesystem::remove_all(mux::config::cache_path("").parent_path(), ignored);
            done();
          },
          [&](mux::config::passphrase_for::change) {
            if (!vault.matches(one.current))
              return root().passphrase_refused("That is not the passphrase now.");
            if (auto refused = fresh_refused())
              return root().passphrase_refused(*refused);
            if (!this->reseal([&](mux::vault::vault& v) { v.begin_change(one.fresh); }))
              return root().passphrase_refused(std::string(vault.resealing() ? kCutShort : kUnread));
            done();
          },
          // A protocol's own: done by its program glue.
          [&](const auto& theirs) { passphrase_given(*this, theirs, one); },
          [&](mux::config::passphrase_for::decrypt) {
            if (!vault.matches(one.current))
              return root().passphrase_refused("That is not the passphrase.");
            if (!this->reseal([](mux::vault::vault& v) { v.begin_decrypt(); }))
              return root().passphrase_refused(std::string(vault.resealing() ? kCutShort : kUnread));
            done();
          }},
      one.why);
}
// From Storage: on asks for a new passphrase, off for the one now.
void app::apply(const request::flip_local_encryption&) {
  if (vault->on())
    root().ask_passphrase(mux::config::passphrase_for::decrypt{});
  else
    root().ask_passphrase(mux::config::passphrase_for::encrypt{});
}
void app::apply(const request::change_passphrase&) { root().ask_passphrase(mux::config::passphrase_for::change{}); }

// From an account's Privacy page: its room keys, to a file or from one.

}  // namespace mux::app
