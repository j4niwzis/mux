// SPDX-License-Identifier: AGPL-3.0-only
// The program: waking on changes, keeping what changed, frames, drafts, the screens shown.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.audio;
import mux.dbus;
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
    splice::visit(splice::overloaded{[&](const mux::change::session_given& given) { this->keep_session(given); },
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
                               // A directory searched: its rooms, in Explore.
                               [&](const mux::change::directory_listed& listed) {
                                 root().show_directory(listed.rooms, listed.server);
                               },
                               // Packs: listed, saved, an image uploaded -- in their dialog.
                               [&](const mux::change::packs_listed& listed) { root().show_packs(listed.packs); },
                               [&](const mux::change::pack_saved& saved) { root().pack_saved(saved.pack, saved.removed, saved.done); },
                               [&](const mux::change::pack_picture_uploaded& uploaded) {
                                 root().pack_picture_uploaded(uploaded.picture, uploaded.done);
                               },
                               // Something the server refused: a notice saying why.
                               [&](const mux::change::refused& said) { root().show_message("Not done", said.what); },
                               // People found: in Start chat, while it asks for them.
                               [&](const mux::change::people_found& found) {
                                 root().show_found_people(found.people, found.query);
                               },
                               [&](const mux::change::state_listed& listed) {
                                 root().show_room_state(listed.entries);
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
                               [](const auto&) {}},
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
    this->keep_on_disk(one);
  }
  // Marks read back from the disk, put in once their chats are here; and
  // the marks written again where they changed.
  std::erase_if(pending_marks, [&](const auto& waiting) {
    if (model->find(waiting.first) == nullptr)
      return false;
    model->apply(waiting.second);
    marks_not_here.erase(waiting.first);
    return true;
  });
  const bool marks_changed = std::ranges::any_of(changes, [](const mux::change_t& one) {
    return splice::visit(splice::overloaded{[](const mux::change::mentioned&) { return true; },
                                      [](const mux::change::reacted_to_mine&) { return true; },
                                      [](const mux::change::reaction_changed& c) { return c.live; },
                                      [](const auto&) { return false; }},
                      one);
  });
  if (marks_changed)
    this->save_marks();
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
                                   // New: said since mux started (a minute's grace for
                                   // clocks) -- not a chat's last messages, which the
                                   // first sync puts at its end too, as tdesktop
                                   // notifies nothing of what it catches up on.
                                   const bool fresh = added.message.at >= started_at - std::chrono::minutes(1);
                                   if (live && fresh && !added.message.outgoing && !added.message.service)
                                     this->notify_of(added.message, mentioning.contains(added.message.id));
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
      const auto room = std::exchange(joining, std::nullopt);
      this->open_chat(*found, room->event);
    }
}

// A message as it came, notified as the settings say: nothing where it is
// in the chat being read with the window focused; else a notification --
// the chat and sender, and the text, as chosen -- and the chime.
void app::notify_of(const mux::message& said, bool mentions_me) {
  if (window_focused && root().main().chosen == said.in)
    return;
  const auto decision = this->notify_for(said.in, mentions_me);
  if (decision.sound)
    mux::audio::play_chime(mux::audio::chime());
  if (!decision.popup)
    return;
  const mux::conversation* chat = model->find(said.in);
  std::string title = "mux";
  if (notifications.show_name && chat) {
    const std::string who = mux::ui::sender_name(*chat, said.sender);
    title = mux::ui::is_group(*chat) ? std::format("{} ({})", who, mux::ui::display_name(*chat)) : who;
  }
  std::string text = "New message";
  if (notifications.show_text) {
    text = said.body.plain.empty() && said.attachment ? std::string("Picture or file") : said.body.plain;
    if (text.size() > 300)
      text = text.substr(0, 300) + "\u2026";
  }
  // Shown by the backend chosen: the desktop's service, asked off the UI's
  // thread; or mux's own window, which comes next -- until then, the log.
  splice::visit(splice::overloaded{[&](mux::config::notify_backend::native) {
                               std::thread([title, text] {
                                 if (!mux::dbus::notify(title, text))
                                   std::println(std::cerr, "[notify] no desktop notification service; {}: {}", title, text);
                               }).detach();
                             },
                             [&](mux::config::notify_backend::built_in) {
                               toasts_due.push_back({said.in, said.in.id, title, text});
                             }},
             mux::config::notify_backend_of(notifications.backend));
}

void app::save_marks() {
  if (ask.demo)
    return;
  mux::config::marks_file out;
  const auto kept = [](const mux::unread_mark& mark) {
    return mux::config::kept_mark{mark.event, mark.target, static_cast<std::int64_t>(mark.at.time_since_epoch().count())};
  };
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations) {
      if (one.unread_mentions.empty() && one.unread_reactions.empty() && one.seen_marks.empty())
        continue;
      mux::config::chat_marks chat{.account = id.address, .conversation = one.id.id};
      if (!one.seen_marks.empty())
        chat.seen = one.seen_marks;
      std::ranges::transform(one.unread_mentions, std::back_inserter(chat.mentions), kept);
      std::ranges::transform(one.unread_reactions, std::back_inserter(chat.reactions), kept);
      out.chats.push_back(std::move(chat));
    }
  // And those of chats not here yet, as they were read.
  for (const auto& [id, chat] : marks_not_here)
    out.chats.push_back(chat);
  std::ofstream(mux::config::state_path("marks.json"), std::ios::binary | std::ios::trunc)
      << knot::to_json_string(out);
}

void app::load_marks() {
  std::ifstream in(mux::config::state_path("marks.json"), std::ios::binary);
  if (!in)
    return;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto read = knot::try_read<mux::config::marks_file>(std::string_view(text));
  if (!read)
    return;
  for (const mux::config::chat_marks& chat : read->chats) {
    const mux::conversation_id id{{mux::ui::protocol_of(chat.account), chat.account}, chat.conversation};
    const auto at = [](std::int64_t ms) {
      return std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(ms));
    };
    marks_not_here.insert_or_assign(id, chat);
    // What was seen first, so that nothing seen comes back as unread.
    if (chat.seen)
      pending_marks.emplace_back(id, mux::change_t{mux::change::marks_seen{id, *chat.seen}});
    for (const auto& mark : chat.mentions)
      pending_marks.emplace_back(id, mux::change_t{mux::change::mentioned{id, mark.event, at(mark.at)}});
    for (const auto& mark : chat.reactions)
      pending_marks.emplace_back(id, mux::change_t{mux::change::reacted_to_mine{id, mark.event, mark.target, at(mark.at)}});
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
    if (splice::visit(splice::overloaded{[](mux::placement::aside) { return true; }, [](const auto&) { return false; }},
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
  splice::visit(splice::overloaded{[&](const mux::change::message_added& c) { added(c); },
                             [&](const mux::change::message_edited& c) { as_now(c.in, c.id); },
                             [&](const mux::change::message_discarded& c) { store.forget(c.in, c.id); },
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
  splice::visit(splice::overloaded{[&](mux::config::matrix_account& one) {
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
                    .kept = this,
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
  // The mentions and reactions for the user whose messages are on screen,
  // seen -- only those, as tdesktop's.
  if (const auto& chosen = root().main().chosen)
    if (const mux::conversation* chat = model->find(*chosen);
        chat && (!chat->unread_mentions.empty() || !chat->unread_reactions.empty()) && !root().open_panel()) {
      auto shown = root().main().shown_now();
      const auto marked = [&](const mux::unread_mark& mark) { return std::ranges::contains(shown, mark.target); };
      if (std::ranges::any_of(chat->unread_mentions, marked) || std::ranges::any_of(chat->unread_reactions, marked)) {
        model->apply(mux::change_t{mux::change::marks_shown{*chosen, std::move(shown)}});
        this->save_marks();
        this->refresh();
      }
    }
  // The input has the keyboard's focus whenever nothing else does -- at the
  // start, after a panel or a search closes -- so typing always goes to it,
  // as in tdesktop.
  if (scene.focusedId() == 0 && root().main().chosen && !root().open_panel() && !root().settings_up())
    scene.focus(root().main().line.field);
  auto pending = std::exchange(ask.requests, {});
  for (const request_t& one : pending)
    splice::visit([this](const auto& each) { this->route(each); }, one);
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

auto app::xmpp_form_up() -> mux::ui::xmpp_form<actions>* {
  auto* up = root().open_panel();
  if (!up)
    return nullptr;
  return splice::visit([](auto& panel) { return panel.xmpp(); }, *up);
}

void app::refresh(std::source_location from) {
  static const bool traced = std::getenv("MUX_TRACE_FRAMES") != nullptr;
  if (traced)
    std::println(std::cerr, "[frame] refresh from {}:{} ({})", std::filesystem::path(from.file_name()).filename().string(),
                 from.line(), from.function_name());
  pictures.ask();
  root().main().muted = muted;
  // Which chats show what is done in them, as the settings say now.
  auto& filters = root().main().event_filters;
  filters.clear();
  for (const auto& [id, account] : model->accounts())
    for (const auto& [key, one] : account.conversations)
      filters.emplace(one.id, this->room_event_filter_of(one.id));
  // The chosen chat's bubbles, as its levels say.
  root().main().bubbles = root().main().chosen ? this->bubbles_of(*root().main().chosen) : mux::config::bubble_look{};
  // The panels' look, as the chosen chat's levels say, else every chat's:
  // the whole window repainted where it changes -- nothing made again.
  mux::ui::panel_look_now() = root().main().chosen ? this->panels_of(*root().main().chosen)
                                                   : panels.value_or(mux::config::bubble_look{});
  if (mux::ui::show_panels(mux::ui::panel_look_now()))
    root().markDamaged();
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

}  // namespace mux::app
