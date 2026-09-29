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
                               // Opened when it comes, in woken().
                               joining = std::visit(
                                   mux::overloaded{[](const mux::logic::link::room& room) { return std::optional(room); },
                                                   [](const auto&) { return std::optional<mux::logic::link::room>(); }},
                                   where);
                               net->join(step.by, step.room, step.via);
                             }},
             mux::logic::where_to(*model, where, screen.chosen, screen.current));
}

void app::apply(const request::cancel_compose&) {
  composing = compose::plain{};
  root().main().line.show_context(std::nullopt);
}

void app::apply(const request::load_older& one) {
  if (ask.demo)
    return;
  const mux::conversation* chat = model->find(one.in);
  const auto before = chat && !chat->timeline.empty() ? chat->timeline.front().at
                                                      : message_store::time_point::max();
  if (auto kept = store.older(one.in, before, 100); !kept.empty()) {
    for (auto it = kept.rbegin(); it != kept.rend(); ++it)
      model->apply(mux::change_t{mux::change::message_added{.message = std::move(*it), .where = mux::placement::at_start{}}});
    // The window may ask again: there may be more on the disk.
    root().main().history_asked.reset();
    this->refresh();
    return;
  }
  net->load_older(one.in, one.from);
}

void app::apply(const request::submit_message& one) { this->send_message(one.text); }

void app::apply(const request::resize_sidebar& one) { root().main().resize_sidebar(one.x); }

void app::apply(const request::message_person& one) {
  if (model->find(one.who) != nullptr)
    this->apply(request::choose{one.who});
  else
    root().show_notice("Starting a new chat");
}

void app::apply(const request::jump_to_message& one) { root().main().jump_to(one.id); }

}  // namespace mux::app
