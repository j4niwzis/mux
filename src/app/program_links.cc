// SPDX-License-Identifier: AGPL-3.0-only
// The program: links followed to rooms, people and messages.
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

void app::open_chat(const mux::conversation_id& which, const std::optional<std::string>& event) {
  auto& screen = root().main();
  screen.current = which.account;
  screen.wanted.reset();
  if (this->last_account != which.account.address) {
    this->last_account = which.account.address;
    (void)this->write();
  }
  this->apply(request::choose{which});
  if (event)
    screen.jump_to(*event);
}

void app::follow(const mux::logic::link_t& where) {
  auto& screen = root().main();
  std::visit(mux::overloaded{[&](const mux::logic::link_step::open_chat& step) { this->open_chat(step.chat, step.event); },
                             [&](const mux::logic::link_step::member_page& step) {
                               this->apply(request::open_member_info{step.user});
                             },
                             [&](const mux::logic::link_step::say& step) { root().show_message(step.title, step.text); },
                             [&](const mux::logic::link_step::join& step) {
                               // Its card first, as a person's: filled when
                               // its server answers, in woken(), and joined
                               // from there.
                               previewing = room_looked_up{
                                   step, std::visit(mux::overloaded{
                                                        [](const mux::logic::link::room& room) { return std::optional(room); },
                                                        [](const auto&) { return std::optional<mux::logic::link::room>(); }},
                                                    where)};
                               root().open_room_card(step.room, mux::room_preview{.note = "Looking it up…"});
                               net->preview_room(step.by, step.room, step.via);
                             }},
             mux::logic::where_to(*model, where, screen.chosen, screen.current));
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
    if (const mux::conversation* in = model->find(*chosen))
      if (const auto aside = in->quoted.find(one.id);
          aside != in->quoted.end() && aside->second.reaction && aside->second.replies_to) {
        root().main().jump_to(*aside->second.replies_to, std::nullopt);
        return;
      }
  root().main().jump_to(one.id, one.fragment);
}

}  // namespace mux::app
