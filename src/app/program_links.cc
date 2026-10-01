// SPDX-License-Identifier: AGPL-3.0-only
// The program: links followed to rooms, people and messages.
module mux.app.program;

import std;
import splice;
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

void app::open_chat(const mux::conversation_id& which, const std::optional<std::string>& event) {
  if (event) {
    this->go_to_message(which, *event, std::nullopt);
    return;
  }
  auto& screen = root().main();
  screen.current = which.account;
  screen.wanted.reset();
  if (this->last_account != which.account.address) {
    this->last_account = which.account.address;
    (void)this->write();
  }
  this->apply(request::choose{which});
}

// A message gone to -- from a reply's quote, a link, a room's own link in
// its messages -- one way for all of them: a chat not open is opened first,
// the one open is not chosen again; a reaction's own line goes to what it
// reacted to; and the view jumps, to the part a quote marks where it has one.
void app::go_to_message(const mux::conversation_id& in, std::string id, std::optional<std::string> fragment) {
  auto& screen = root().main();
  // Where it goes from, to come back to: this chat, where it goes to
  // another; else the place in this one.
  const bool here = screen.chosen && *screen.chosen == in;
  if (!here) {
    screen.note_chat_return();
    this->open_chat(in, std::nullopt);
  }
  if (const mux::conversation* chat = model->find(in))
    if (const auto aside = chat->quoted.find(id);
        aside != chat->quoted.end() && aside->second.reaction && aside->second.replies_to) {
      id = *aside->second.replies_to;
      fragment.reset();
    }
  // As tdesktop's doneJumpFrom: a way back to where it goes is done with,
  // then where it went from is one.
  if (here) {
    screen.skip_return(id);
    screen.note_return();
  }
  screen.jump_to(std::move(id), std::move(fragment));
}

void app::follow(const mux::logic::link_t& where) {
  auto& screen = root().main();
  splice::visit(splice::overloaded{[&](const mux::logic::link_step::open_chat& step) { this->open_chat(step.chat, step.event); },
                             [&](const mux::logic::link_step::member_page& step) {
                               this->apply(request::open_member_info{step.user});
                             },
                             [&](const mux::logic::link_step::say& step) { root().show_message(step.title, step.text); },
                             [&](const mux::logic::link_step::join& step) {
                               // Its card first, as a person's: filled when
                               // its server answers, in woken(), and joined
                               // from there.
                               previewing = room_looked_up{
                                   step, splice::visit(splice::overloaded{
                                                        [](const mux::logic::link::room& room) { return std::optional(room); },
                                                        [](const auto&) { return std::optional<mux::logic::link::room>(); }},
                                                    where)};
                               root().open_room_card(step.room, mux::room_preview{.note = "Looking it up…"});
                               net->preview_room(step.by, step.room, step.via);
                             }},
             mux::logic::where_to(*model, where, screen.current));
}

// The room of the card joined: opened when it comes, in woken().
void app::apply(const request::knock_room_card&) {
  if (!previewing)
    return;
  const auto looked = *std::exchange(previewing, std::nullopt);
  net->knock(looked.step.by, looked.step.room, looked.step.via, std::string());
  root().close_room_card();
  root().show_notice("Asked to join. You'll be let in once someone in the room accepts.");
}
void app::apply(const request::join_room_card&) {
  if (!previewing)
    return;
  const auto looked = *std::exchange(previewing, std::nullopt);
  joining = looked.link;
  net->join(looked.step.by, looked.step.room, looked.step.via);
  root().close_room_card();
}

// A mark's message gone to: in its thread, where it is an answer in one --
// the thread opened beside the chat, and the mark let go, the message being
// shown there -- else in the timeline, straight to what is around it, as a
// reply's quote goes. There the mark is let go only once the message is on
// the screen (marks_shown): a jump cancelled, or still on its way, leaves it.
// Paged back to in the timeline, an answer in a thread was looked for there
// for ever.
void app::go_to_marked(const mux::conversation& chat, mux::mark_kind_t kind, const std::string& event, const std::string& target) {
  if (const mux::message* said = mux::ui::held_message(chat, target); said && said->thread) {
    auto& screen = root().main();
    screen.open_thread(*said->thread);
    if (!shared.demo())
      net->load_thread(chat.id, *said->thread);
    model->apply(mux::change_t{mux::change::mark_taken{chat.id, kind, event}});
    this->save_marks();
    this->refresh();
    return;
  }
  root().main().jump_to(target);
  this->refresh();
}

// Telegram's @ and heart: the oldest mention or reaction not yet seen, gone
// to; let go once it is seen.
void app::apply(const request::jump_to_mark& one) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (chat == nullptr)
    return;
  const auto& marks =
      splice::visit(splice::overloaded{[&](mux::mark_kind::mention) -> const std::vector<mux::unread_mark>& { return chat->unread_mentions; },
                                 [&](mux::mark_kind::reaction) -> const std::vector<mux::unread_mark>& { return chat->unread_reactions; }},
                 one.kind);
  if (marks.empty())
    return;
  // The oldest first, as Telegram goes through them: by when each came, not
  // by the order they were learned in -- the ones caught up after a restart
  // come in after newer ones.
  const auto oldest = std::ranges::min_element(marks, {}, &mux::unread_mark::at);
  const std::string target = oldest->target, event = oldest->event;
  this->go_to_marked(*chat, one.kind, event, target);
}

// All of them, listed as the chat's bubbles: each mention's message, each
// reaction's with who and what -- one not here yet, fetched on its own.
void app::apply(const request::list_marks& one) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (chat == nullptr)
    return;
  const auto& marks =
      splice::visit(splice::overloaded{[&](mux::mark_kind::mention) -> const std::vector<mux::unread_mark>& { return chat->unread_mentions; },
                                 [&](mux::mark_kind::reaction) -> const std::vector<mux::unread_mark>& { return chat->unread_reactions; }},
                 one.kind);
  // In the timeline, in a thread, or aside.
  const auto find = [&](const std::string& id) -> const mux::message* { return mux::ui::held_message(*chat, id); };
  std::vector<mux::ui::mark_entry> entries;
  for (const mux::unread_mark& mark : marks) {
    mux::ui::mark_entry entry{.event = mark.event};
    if (const mux::message* said = find(mark.target)) {
      entry.said = *said;
      if (const auto reacted = std::ranges::find(said->reaction_events, mark.event, &mux::message::reaction_event::event);
          reacted != said->reaction_events.end()) {
        entry.who = reacted->who;
        entry.key = reacted->key;
      }
    } else {
      entry.said = mux::message{.in = *chosen, .id = mark.target, .at = mark.at, .body = {"Loading…", std::nullopt}};
      if (!shared.demo())
        net->fetch_quoted(*chosen, mark.target);
    }
    entries.push_back(std::move(entry));
  }
  root().open_marks(one.kind, *chat, entries, &*model);
}
// One of the list, gone to, and let go.
void app::apply(const request::go_to_mark& one) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (chat == nullptr)
    return;
  const auto& marks =
      splice::visit(splice::overloaded{[&](mux::mark_kind::mention) -> const std::vector<mux::unread_mark>& { return chat->unread_mentions; },
                                 [&](mux::mark_kind::reaction) -> const std::vector<mux::unread_mark>& { return chat->unread_reactions; }},
                 one.kind);
  const auto found = std::ranges::find(marks, one.event, &mux::unread_mark::event);
  if (found == marks.end())
    return;
  const std::string target = found->target;
  this->go_to_marked(*chat, one.kind, one.event, target);
}
void app::apply(const request::close_marks&) { root().close_marks(); }

// The invite of the card declined: the room left, the card closed.
void app::apply(const request::decline_room_card&) {
  if (!previewing)
    return;
  const auto looked = *std::exchange(previewing, std::nullopt);
  net->leave(mux::conversation_id{looked.step.by, looked.step.room});
  root().close_room_card();
}

void app::apply(const request::close_room_card&) {
  previewing.reset();
  root().close_room_card();
}

void app::apply(const request::load_context& one) {
  if (!ask.demo)
    net->load_context(one.in, one.target);
}

void app::apply(const request::load_newer& one) {
  if (!ask.demo)
    net->load_newer(one.in, one.from);
}

void app::apply(const request::load_older& one) {
  if (ask.demo)
    return;
  const mux::conversation* chat = model->find(one.in);
  const auto before = chat && !chat->timeline.empty() ? chat->timeline.front().at
                                                      : message_store::time_point::max();
  // From the disk first -- read on a worker -- but not before a window of the
  // history, whose messages the disk may not have up to: from the server,
  // from its token, where the disk has none.
  if (chat && chat->detached) {
    net->load_older(one.in, one.from);
    return;
  }
  work.run([this, in = one.in, from = one.from, before]() -> workers::done_t {
    auto kept = message_store::older(in, before, 100);
    return [this, in, from, kept = std::move(kept)]() mutable {
      if (kept.empty()) {
        net->load_older(in, from);
        return;
      }
      for (auto it = kept.rbegin(); it != kept.rend(); ++it)
        model->apply(mux::change_t{mux::change::message_added{.message = std::move(*it), .where = mux::placement::at_start{}}});
      // The window may ask again: there may be more on the disk.
      root().main().history_asked.reset();
      this->refresh();
    };
  });
}

void app::apply(const request::resize_sidebar& one) { root().main().resize_sidebar(one.x); }

void app::apply(const request::message_person& one) {
  if (model->find(one.who) != nullptr) {
    this->apply(request::choose{one.who});
    return;
  }
  // No chat with them yet: one started, as Start chat starts it -- their
  // card closed, the chat opened once it is made.
  this->apply(request::close_person_info{});
  this->apply(request::start_direct{one.who.id});
}

// A quote pressed: to what it quotes -- or, a reaction's, to the message it
// reacted to, as any quote's.
void app::apply(const request::jump_to_message& one) {
  // An answer in a thread, or the root of the thread open: there, in the
  // thread's panel -- its quote pressed in the thread went nowhere.
  if (const auto& chosen = root().main().chosen)
    if (const mux::conversation* chat = model->find(*chosen))
      if (const mux::message* said = mux::ui::held_message(*chat, one.id)) {
        auto& screen = root().main();
        const std::optional<std::string> thread = said->thread ? said->thread
                                                  : screen.thread_open() == said->id ? std::optional<std::string>(said->id)
                                                                                      : std::nullopt;
        if (thread) {
          if (screen.thread_open() != thread) {
            screen.open_thread(*thread);
            if (!shared.demo())
              net->load_thread(chat->id, *thread);
            this->refresh();
          }
          if (screen.parts.threads.scroll_to(one.id))
            return;
        }
      }
  if (const auto& chosen = root().main().chosen) {
    root().main().return_from = one.from;
    this->go_to_message(*chosen, one.id, one.fragment);
    root().main().return_from.reset();
  }
}

}  // namespace mux::app
