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
  this->take_push();
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
                                 if (updated.invite && invites_told.insert(updated.id).second)
                                   this->notify_invite(updated.id, *updated.invite, updated.name);
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
  // A mark made: its message kept with it, whole, as it is now -- the list of
  // marks shows it from there, never "Loading…".
  if (!ask.demo)
    for (const mux::change_t& one : changes)
      splice::visit(splice::overloaded{[&](const mux::change::mentioned& m) { this->keep_marked(m.in, m.event); },
                                       [&](const mux::change::reacted_to_mine& r) { this->keep_marked(r.in, r.target); },
                                       [](const auto&) {}},
                    one);
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
  // Marked messages fetched: kept with their marks now.
  for (auto it = marked_wanted_.begin(); it != marked_wanted_.end();) {
    const mux::conversation* chat = model->find(it->second);
    const mux::message* said = chat ? mux::ui::held_message(*chat, it->first) : nullptr;
    if (said == nullptr) {
      ++it;
      continue;
    }
    marked_kept_.insert(it->first);
    store.keep_marked(it->second, *said);
    it = marked_wanted_.erase(it);
  }
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
  // The list of marks open, shown again where what it waited for came -- a
  // message fetched for it, or the word that it is not there (its mark
  // then gone): it said "Loading…" until it was opened again.
  if (marks_listed_ && root().marks_up()) {
    const bool waited = std::ranges::any_of(changes, [](const mux::change_t& one) {
      return splice::visit(splice::overloaded{[](const mux::change::message_added& c) {
                                                return splice::visit(splice::overloaded{[](mux::placement::aside) { return true; },
                                                                                        [](const auto&) { return false; }},
                                                                     c.where);
                                              },
                                              [](const mux::change::event_missing&) { return true; },
                                              [](const auto&) { return false; }},
                           one);
    });
    if (waited)
      this->apply(request::list_marks{*marks_listed_});
  } else {
    marks_listed_.reset();
  }
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

// A message as it came, notified as the settings say: nothing where it is
// in the chat being read with the window focused; else a notification --
// the chat and sender, and the text, as chosen -- and the chime.
void app::notify_of(const mux::message& said, bool mentions_me) {
  if (on_screen && window_focused && root().main().chosen == said.in)
    return;
  const auto decision = this->notify_for(said.in, mentions_me);
  if (decision.sound)
    chimes.play();
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
    if (text.size() > 300) {
      // Cut where a character starts: half of one is not UTF-8, and the bus
      // drops a connection that sends it.
      std::size_t cut = 300;
      while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
      text = text.substr(0, cut) + "\u2026";
    }
  }
  // Shown by the backend chosen: the desktop's service, asked off the UI's
  // thread; or mux's own window, which comes next -- until then, the log.
  splice::visit(splice::overloaded{[&](mux::config::notify_backend::native) {
                               std::thread([title, text] {
                                 if (!mux::platform::notifications::notify(title, text))
                                   std::println(std::cerr, "[notify] no desktop notification service; {}: {}", title, text);
                               }).detach();
                             },
                             [&](mux::config::notify_backend::built_in) {
                               toasts_due.push_back({said.in, said.in.id, title, text});
                             }},
             mux::config::notify_backend_of(notifications.backend));
}

// An invite, as a notification: who asked, and to what -- by the backend
// chosen, with the chime, as the settings say for its account.
void app::notify_invite(const mux::conversation_id& in, const mux::invite_info& invite, const std::string& name) {
  const auto decision = this->notify_for(in, true);
  if (decision.sound)
    chimes.play();
  if (!decision.popup)
    return;
  const std::string who = invite.from_name.empty() ? invite.from : invite.from_name;
  const std::string title = invite.direct ? std::format("{} invites you to chat", who) : std::format("Invite to {}", name);
  const std::string text = invite.direct ? std::string("A direct chat") : std::format("from {}", who);
  splice::visit(splice::overloaded{[&](mux::config::notify_backend::native) {
                                     std::thread([title, text] {
                                       if (!mux::platform::notifications::notify(title, text))
                                         std::println(std::cerr, "[notify] no desktop notification service; {}: {}", title, text);
                                     }).detach();
                                   },
                                   [&](mux::config::notify_backend::built_in) {
                                     toasts_due.push_back({in, in.id, title, text});
                                   }},
                mux::config::notify_backend_of(notifications.backend));
}

void app::keep_marked(const mux::conversation_id& in, const std::string& id) {
  if (!marked_kept_.insert(id).second)
    return;
  if (const mux::conversation* chat = model->find(in))
    if (const mux::message* said = mux::ui::held_message(*chat, id)) {
      store.keep_marked(in, *said);
      return;
    }
  // Kept already, in an earlier run: the marks read back at the start are
  // not fetched again.
  auto on_disk = marked_on_disk_.find(in);
  if (on_disk == marked_on_disk_.end())
    on_disk = marked_on_disk_
                  .emplace(in, store.marked(in) | std::views::keys | std::ranges::to<std::set<std::string>>())
                  .first;
  if (on_disk->second.contains(id))
    return;
  // Not here yet: kept once it comes (quoted, in woken), fetched now.
  marked_kept_.erase(id);
  marked_wanted_.insert_or_assign(id, in);
  net->fetch_quoted(in, id);
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
  (void)vault->write_file(mux::config::state_path("marks.json"), knot::to_json_string(out));
}

void app::load_marks() {
  const auto opened = vault->read_file(mux::config::state_path("marks.json"));
  if (!opened)
    return;
  const std::string& text = *opened;
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

message_store::gaps_t& app::gaps_of(const mux::conversation_id& in) {
  auto found = gaps_.find(in);
  if (found == gaps_.end()) {
    auto read = store.gaps(in);
    if (read)
      gaps_kept_before_.insert(in);
    found = gaps_.emplace(in, read.value_or(message_store::gaps_t{})).first;
  }
  return found->second;
}
void app::gaps_changed(const mux::conversation_id& in) {
  gaps_kept_before_.insert(in);
  store.keep_gaps(in, gaps_of(in));
}

void app::keep_on_disk(const mux::change_t& one) {
  if (ask.demo)
    return;
  // Where the history on disk has gaps, noted as it comes (see
  // message_store::gap_mark). A page from the server: the message it was
  // paged from now follows it, and before its oldest is the token it ended
  // at -- or the room's beginning. A first sync: before its first message
  // is its prev_batch. A window loaded around a message: its oldest, not
  // known to follow anything on disk. Too many marks cost only a request.
  splice::visit(
      splice::overloaded{
          [&](const mux::change::history_position& c) {
            const mux::conversation* chat = model->find(c.in);
            if (const auto paged = paging_from_.find(c.in); paged != paging_from_.end()) {
              auto& gaps = gaps_of(c.in);
              gaps.erase(paged->second);
              // The page reached what the disk had before it was asked for
              // (on_disk_, read then): the gap is closed, and the disk goes
              // on from there -- else every run's first page back left a
              // gap past which everything was asked of the server again.
              const auto known = on_disk_.find(c.in);
              const bool overlapped = chat && !chat->timeline.empty() && known != on_disk_.end() &&
                                      known->second.contains(chat->timeline.front().id);
              if (chat && !chat->timeline.empty() && !overlapped)
                gaps.insert_or_assign(chat->timeline.front().id,
                                      message_store::gap_mark{.token = c.from, .start = !c.from.has_value()});
              paging_from_.erase(paged);
              this->gaps_changed(c.in);
            } else {
              sync_gap_.insert_or_assign(c.in, c.from);
            }
          },
          [&](const mux::change::message_added& c) {
            splice::visit(splice::overloaded{
                              [&](mux::placement::at_end) {
                                if (const auto pending = sync_gap_.find(c.message.in); pending != sync_gap_.end()) {
                                  gaps_of(c.message.in)
                                      .insert_or_assign(c.message.id, message_store::gap_mark{.token = pending->second,
                                                                                              .start = !pending->second});
                                  sync_gap_.erase(pending);
                                  this->gaps_changed(c.message.in);
                                }
                              },
                              [&](mux::placement::in_window) {
                                // A window over history already on disk -- a jump back
                                // to what was read before -- leaves no gap: what is
                                // before it is there already, as it was. Only one
                                // over what the disk did not have does.
                                auto known = on_disk_.find(c.message.in);
                                if (known == on_disk_.end())
                                  known = on_disk_
                                              .emplace(c.message.in, store.everything(c.message.in) | std::views::keys |
                                                                         std::ranges::to<std::set<std::string>>())
                                              .first;
                                const bool was_kept = !known->second.insert(c.message.id).second;
                                const mux::conversation* chat = model->find(c.message.in);
                                if (!was_kept && chat && !chat->timeline.empty() && chat->timeline.front().id == c.message.id) {
                                  gaps_of(c.message.in).insert_or_assign(c.message.id, message_store::gap_mark{});
                                  this->gaps_changed(c.message.in);
                                }
                              },
                              [](const auto&) {}},
                          c.where);
          },
          [](const auto&) {}},
      one);
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
                             [&](const mux::change::message_encrypted& c) { as_now(c.in, c.id); },
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
                    .system_dialogs = &system_dialogs};
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
        this->save_marks();
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
  this->load_marks();
  this->notifications = saved.notifications.value_or(mux::config::notification_settings{});
  // UnifiedPush only where chosen; off by default.
  if (notifications.unified_push.value_or(false) && !demo)
    this->start_push();
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
            if (auto loaded = mux::config::load(config_path, *vault))
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
// Emoji verification: begun from a person's card or a session's row; its
// dialog's answers, to the verification it shows.
// Element's Secure Backup and Sessions: the backup made anew or deleted,
// for the account whose page is open; one's unverified sessions signed out,
// the password asked first.
void app::apply(const request::verify_person& one) { net->verify_start(one.who.account, one.who.id, std::nullopt); }
void app::apply(const request::verify_accept_now&) {
  if (verifying)
    net->verify_accept(verifying->first, verifying->second);
}
void app::apply(const request::verify_cancel_now&) {
  if (verifying)
    net->verify_cancel(verifying->first, verifying->second);
  root().close_verification();
}
void app::apply(const request::verify_match&) {
  if (verifying)
    net->verify_confirm(verifying->first, verifying->second, true);
}
void app::apply(const request::verify_mismatch&) {
  if (verifying)
    net->verify_confirm(verifying->first, verifying->second, false);
}
void app::apply(const request::close_verification&) {
  verifying.reset();
  root().close_verification();
}

// From an account's Privacy page: its room keys, to a file or from one.

}  // namespace mux::app
