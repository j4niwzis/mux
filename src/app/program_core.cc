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
                               // A call's: the calls part's.
                               [&](const mux::change::call_signalled& said) { calls.take(said); },
                               [&](const mux::change::call_servers& given) { calls.take(given); },
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
                                                      mux::ui::person_of(shared.ui, model->find(chat), *model, chat.account, user));
                                 }
                               },
                               [&](const mux::change::trust_changed& told) {
                                 if (person_open_ && person_open_->first.account == told.by && person_open_->second == told.user) {
                                   const auto [chat, user] = *person_open_;
                                   root().open_person(chat.account, user,
                                                      mux::ui::person_of(shared.ui, model->find(chat), *model, chat.account, user));
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
                                     !room_card.looking_at(shown.asked)) {
                                   if (!shown.preview.id.empty()) {
                                     model->rooms_found.insert_or_assign(shown.asked, shown.preview.name);
                                     if (shown.preview.avatar)
                                       net->fetch_avatar(shown.by, *shown.preview.avatar, shown.asked);
                                   }
                                   return;
                                 }
                                 room_card.previewed(shown);
                               },
                               [&](const mux::change::media_progress& how) {
                                 mux::ui::download_progress().insert_or_assign(how.source, how.done);
                                 scene.state().markDamaged();
                               },
                               // What an account's protocol is now: what is offered of it.
                               [&](const mux::change::protocol_state_changed& now) {
                                 shared.ui.protocol_states.insert_or_assign(now.account, now.now);
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
  if (shared.joining)
    if (const auto found = mux::logic::chat_of(*model, *shared.joining)) {
      const auto link = *std::exchange(shared.joining, std::nullopt);
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
  // What the parts reach, set in place: services holds the program's own
  // state too (the looks, the paint, what the window's parts share), which
  // the window was made pointing at -- made anew, it was all lost.
  shared.model = model;
  shared.net = net;
  shared.store = &store;
  shared.box = box;
  shared.ask = &ask;
  shared.scene = &scene;
  shared.kept = this;
  shared.vault = vault;
  shared.work = &work;
  work.wake_with(wake.kind);
  shared.system_dialogs = &system_dialogs;
  shared.wake = &wake;
  shared.speaker = &speaker;
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
  if (scene.focusedId() == 0 && !shared.by_touch && root().main().chosen && !root().open_panel() && !root().settings_up())
    scene.focus(root().main().line.field);
  // The emoji panel's stickers in view, asked for as they change -- as it
  // opens, as it scrolls, as its tab or its search changes -- and not only
  // when something else refreshed the window.
  if (auto shown = root().emoji_pictures_shown(); shown != shared.ui.panel_pictures_shown) {
    shared.ui.panel_pictures_shown = std::move(shown);
    shared.ui.pictures_due = true;
  }
  calls.tick();
  menu.keep_selection();
  auto pending = std::exchange(ask.requests, {});
  for (const request_t& one : pending)
    splice::visit([this](const auto& each) { this->route(each); }, one);
  // A selectable text or a field pressed with the right button -- a long
  // press, on a phone: its menu, the last asked for.
  if (auto asked = std::exchange(skiff::scene::textMenusAsked(), {}); !asked.empty() && !root().context_menu_up())
    splice::visit(splice::overloaded{[&](skiff::scene::text_menu::of_text& text) {
                                       root().show_text_menu(std::move(text.text), std::move(text.link));
                                     },
                                     [&](const skiff::scene::text_menu::of_field& field) { root().show_field_menu(field); }},
                  asked.back());
  // What the parts left to do: a chat opened, a link followed; the window
  // made again, brought up to date; the emoji picked lately kept.
  if (auto chat = std::exchange(shared.chat_due, std::nullopt))
    this->open_chat(*chat, std::nullopt);
  if (auto link = std::exchange(shared.link_due, std::nullopt))
    this->follow(*link);
  if (std::exchange(shared.rebuild_due, false))
    this->rebuild_in_theme();
  if (std::exchange(shared.refresh_due, false))
    this->refresh();
  if (std::exchange(shared.ui.pictures_due, false))
    pictures.ask();
  if (std::exchange(shared.emoji.emoji_changed, false)) {
    recent_emoji = shared.emoji.recent_emoji;
    (void)this->write();
  }
  if (std::exchange(shared.emoji.stickers_changed, false)) {
    recent_stickers = shared.emoji.recent_stickers;
    favourite_stickers = shared.emoji.favourite_stickers;
    (void)this->write();
  }
  if (shared.drawer_waits && !root().pages_moving()) {
    root().close_drawer_now();
    shared.drawer_waits = false;
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
  accounts_screen.forget_login();
  root().close();
  this->refresh();
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
  this->note_spaces();
  this->show_placements();
  this->show_event_filters();
  this->show_looks_now();
  this->show_space_bars();
  this->show_levels();
  this->show_backgrounds();
  this->show_chat_choices();
  root().show(saved, *model);
  root().main().show(*model);
  // The newly made range is now known, including a just-opened chat.
  pictures.ask();
  // The accounts page, where it is up: the account being added shown in,
  // and the chats then.
  if (auto* up = root().open_panel())
    if (splice::visit([this](auto& panel) { return accounts_screen.bring_up_to_date(panel); }, *up))
      this->show_conversations();
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
  mux::app::tell_protocol_ops(shared.ui, mux::protocols{});
  // The settings, as kept.
  this->take(saved);
  // And what of them the window holds, put in place there.
  shared.emoji.recent_emoji = this->recent_emoji;
  shared.emoji.recent_stickers = this->recent_stickers;
  shared.emoji.favourite_stickers = this->favourite_stickers;
  if (saved.last_account)
    this->root().main().wanted = mux::account_id{mux::ui::protocol_of(*saved.last_account), *saved.last_account};
  shared.looks.bubbles_everywhere = this->bubbles.value_or(mux::config::bubble_look{});
  shared.looks.panels_everywhere = this->panels.value_or(mux::config::bubble_look{});
  this->wallpaper_behind = shared.looks.window.behind;
  shared.looks.window.live_blur = saved.live_blur.value_or(false);
  this->live_blur = shared.looks.window.live_blur;
  this->frost_blur = shared.looks.window.frost;
  shared.looks.window.home_hides = this->home_hides_spaced;
  shared.looks.window.home_direct = this->home_hides_direct;
  shared.looks.window.spaces = this->spaces;
  shared.looks.window.top_bar = this->top_bar;
  this->interface_scale = std::clamp(this->interface_scale, mux::ui::kScaleLeast, mux::ui::kScaleMost);
  shared.looks.window.interface_scale = this->interface_scale;
  if (!demo)
    this->drafts.load();
  this->model->show_deleted = this->history.show_deleted;
  this->settings.apply_limits();
  marks.load();
  // UnifiedPush only where chosen; off by default.
  if (notifications.unified_push.value_or(false) && !demo)
    notices.start_push();
  skiff::paint::motionLevel() = motion_of(saved.motion);
  this->root().show_motion(saved.motion.value_or("full"));
  this->config_error = std::move(error);
  this->refresh();
}

void app::lock(std::vector<mux::config::account_t> extra, bool demo) { local_data.lock(std::move(extra), demo); }

// A passphrase given, by what it was asked for: local data's, to its part
// -- opened, what was kept begun -- and a protocol's own, to its program
// glue.
void app::apply(const request::give_passphrase& one) {
  splice::visit(splice::overloaded{[&](mux::config::passphrase_for::unlock) {
                                     if (auto opened = local_data.unlock(one))
                                       this->begin(opened->saved, std::move(opened->extra), opened->demo, std::move(opened->error));
                                   },
                                   [&](mux::config::passphrase_for::encrypt) { local_data.encrypt(one); },
                                   [&](mux::config::passphrase_for::change) { local_data.change(one); },
                                   [&](mux::config::passphrase_for::decrypt) { local_data.decrypt(one); },
                                   [&](const auto& theirs) { passphrase_given(*this, theirs, one); }},
                one.why);
}

// From an account's Privacy page: its room keys, to a file or from one.

}  // namespace mux::app
