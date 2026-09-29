// SPDX-License-Identifier: AGPL-3.0-only
// The program: waking on changes, keeping what changed, frames, drafts, the screens shown.
module mux.app.program;

import std;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.logic.links;

namespace mux::app {

auto app::window() -> skiff::scene::Scene<window_type>& { return scene; }

void app::woken() {
  // What the workers made, put where it goes: on this, the UI's thread.
  work.finish();
  auto changes = box->take();
  if (changes.empty())
    return;
  for (const auto& one : changes) {
    // What the program itself does with a change, besides the model: a
    // session kept, a picture shown.
    std::visit(mux::overloaded{[&](const mux::change::session_given& given) { this->keep_session(given); },
                               [&](const mux::change::avatar_loaded& picture) {
                                 pictures.take(picture, true);
                                 mux::ui::download_progress().erase(picture.source);
                               },
                               // A room the user made: shown, once the model has it.
                               [&](const mux::change::room_created& made) { made_room_ = made.id; },
                               // What the developer tools asked, shown.
                               [&](const mux::change::devtools_text& shown) {
                                 root().show_devtools_text(shown.title, shown.text);
                               },
                               [&](const mux::change::state_listed& listed) {
                                 root().show_room_state(listed.entries);
                               },
                               // A room looked up: its card filled, while it
                               // is up for that room still.
                               [&](const mux::change::room_previewed& shown) {
                                 if (!previewing || !root().room_card_up() || previewing->step.by != shown.by ||
                                     previewing->step.room != shown.asked)
                                   return;
                                 if (shown.preview.avatar && !shown.preview.id.empty())
                                   net->fetch_avatar(shown.by, *shown.preview.avatar, shown.preview.id);
                                 root().open_room_card(shown.asked, shown.preview);
                               },
                               [&](const mux::change::media_progress& how) {
                                 mux::ui::download_progress().insert_or_assign(how.source, how.done);
                                 scene.state().markDamaged();
                               },
                               [](const auto&) {}},
               one);
    // A message deleted: marked where it is kept, and kept whole apart --
    // as it was, before the model takes it out of view.
    if (!ask.demo)
      std::visit(mux::overloaded{[&](const mux::change::message_redacted& c) {
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
    this->keep_on_disk(one);
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
      const auto room = std::exchange(joining, std::nullopt);
      this->open_chat(*found, room->event);
    }
}

void app::keep_on_disk(const mux::change_t& one) {
  if (ask.demo)
    return;
  const auto as_now = [&](const mux::conversation_id& in, const std::string& id) {
    if (const mux::conversation* chat = model->find(in))
      if (const auto found = std::ranges::find(chat->timeline, id, &mux::message::id); found != chat->timeline.end())
        store.record(*found);
  };
  // A message added is kept as it is now in the timeline -- or, where it is
  // not in it (it came live while the chat is a window elsewhere), as it
  // came: kept either way, or it would be lost on going back to the newest.
  const auto added = [&](const mux::change::message_added& c) {
    // A message fetched for a quote is not history read in order: kept on
    // disk, it would be read back as though it were next to the rest.
    if (std::visit(mux::overloaded{[](mux::placement::aside) { return true; }, [](const auto&) { return false; }},
                   c.where))
      return;
    const mux::conversation* chat = model->find(c.message.in);
    const bool in_timeline =
        chat && std::ranges::find(chat->timeline, c.message.id, &mux::message::id) != chat->timeline.end();
    if (in_timeline)
      as_now(c.message.in, c.message.id);
    else if (!c.message.id.empty())
      store.record(c.message);
  };
  std::visit(mux::overloaded{[&](const mux::change::message_added& c) { added(c); },
                             [&](const mux::change::message_edited& c) { as_now(c.in, c.id); },
                             [&](const mux::change::reaction_changed& c) { as_now(c.in, c.id); },
                             [&](const mux::change::receipts_changed& c) {
                               if (const mux::conversation* chat = model->find(c.in))
                                 store.keep_reads(c.in, *chat);
                             },
                             [&](const mux::change::message_acknowledged& c) {
                               store.forget(c.in, c.local_id);
                               as_now(c.in, c.id);
                             },
                             [](const auto&) {}},
             one);
}

void app::keep_session(const mux::change::session_given& given) {
  const auto found = this->find(given.account.address);
  if (found == saved.end())
    return;
  std::visit(mux::overloaded{[&](mux::config::matrix_account& one) {
                               one.access_token = given.access_token;
                               one.device_id = given.device_id;
                             },
                             [](mux::config::xmpp_account&) {}},
             *found);
  (void)this->write();
}

void app::wire() {
  shared = services{.model = model,
                    .net = net,
                    .store = &store,
                    .box = box,
                    .ask = &ask,
                    .scene = &scene,
                    .refresh = [this] { this->refresh(); },
                    .settings_of = [this](std::string_view address) { return this->settings_of(address); },
                    .go_live = [this](const mux::conversation_id& in) { this->go_live(in); },
                    .work = &work};
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
  // The input has the keyboard's focus whenever nothing else does -- at the
  // start, after a panel or a search closes -- so typing always goes to it,
  // as in tdesktop.
  if (scene.focusedId() == 0 && root().main().chosen && !root().open_panel() && !root().settings_up())
    scene.focus(root().main().line.field);
  auto pending = std::exchange(ask.requests, {});
  for (const request_t& one : pending)
    std::visit([this](const auto& each) { this->route(each); }, one);
  if (drawer_waits && !root().pages_moving()) {
    root().close_drawer_now();
    drawer_waits = false;
  }
}

void app::closing() {
  if (const auto& chosen = root().main().chosen)
    drafts.keep(*chosen, root().main().line.text());
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

auto app::xmpp_form_up() -> mux::ui::xmpp_form<actions>* {
  auto* up = root().open_panel();
  if (!up)
    return nullptr;
  return std::visit([](auto& panel) { return panel.xmpp(); }, *up);
}

void app::refresh() {
  pictures.ask();
  root().main().muted = muted;
  root().show(saved, *model);
  root().main().show(*model);
  if (auto* up = root().open_panel())
    std::visit([this](auto& panel) { this->bring_up_to_date(panel); }, *up);
}

void app::bring_up_to_date(accounts& panel) {
  panel.proxies = proxies;
  panel.show(saved, *model);
  if (auto* pane = panel.adding())
    std::visit([this](auto& form) { this->watch_login(form); }, pane->parts.form);
}

}  // namespace mux::app
