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
  if (!screen.chosen || *screen.chosen != in)
    this->open_chat(in, std::nullopt);
  if (const mux::conversation* chat = model->find(in))
    if (const auto aside = chat->quoted.find(id);
        aside != chat->quoted.end() && aside->second.reaction && aside->second.replies_to) {
      id = *aside->second.replies_to;
      fragment.reset();
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
void app::apply(const request::join_room_card&) {
  if (!previewing)
    return;
  const auto looked = *std::exchange(previewing, std::nullopt);
  joining = looked.link;
  net->join(looked.step.by, looked.step.room, looked.step.via);
  root().close_room_card();
}

// Telegram's @ and heart: the oldest mention or reaction not yet seen, gone
// to -- straight to what is around it, as a reply's quote goes -- and let go.
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
  const std::string target = oldest->target;
  model->apply(mux::change_t{mux::change::mark_taken{*chosen, one.kind, oldest->event}});
  this->save_marks();
  root().main().jump_to(target);
  this->refresh();
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
  const auto find = [&](const std::string& id) -> const mux::message* {
    if (const auto at = std::ranges::find(chat->timeline, id, &mux::message::id); at != chat->timeline.end())
      return &*at;
    if (const auto aside = chat->quoted.find(id); aside != chat->quoted.end())
      return &aside->second;
    return nullptr;
  };
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
  model->apply(mux::change_t{mux::change::mark_taken{*chosen, one.kind, one.event}});
  this->save_marks();
  root().main().jump_to(target);
  this->refresh();
}
void app::apply(const request::close_marks&) { root().close_marks(); }

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
  if (model->find(one.who) != nullptr)
    this->apply(request::choose{one.who});
  else
    root().show_notice("Starting a new chat");
}

// A quote pressed: to what it quotes -- or, a reaction's, to the message it
// reacted to, as any quote's.
void app::apply(const request::jump_to_message& one) {
  if (const auto& chosen = root().main().chosen)
    this->go_to_message(*chosen, one.id, one.fragment);
}

}  // namespace mux::app
