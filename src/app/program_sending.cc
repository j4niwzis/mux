// SPDX-License-Identifier: AGPL-3.0-only
// The program: messages sent and the motion setting.
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

namespace mux::app {

void app::send_message(std::string text) {
  auto& screen = root().main();
  const auto blank = [](unsigned char c) { return std::isspace(c) != 0; };
  if (!screen.chosen || std::ranges::all_of(text, blank))
    return;
  const mux::conversation_id to = *screen.chosen;
  std::visit(mux::overloaded{[&](const compose::plain&) { ask.send(to, std::move(text)); },
                             [&](const compose::reply& one) {
                               if (ask.demo)
                                 ask.send(to, std::move(text));
                               else
                                 net->send(to, std::move(text), one.id);
                             },
                             [&](const compose::edit& one) {
                               if (ask.demo)
                                 box->push(mux::change_t{
                                     mux::change::message_edited{to, one.id, mux::body{std::move(text), std::nullopt}}});
                               else
                                 net->edit(to, one.id, std::move(text));
                             }},
             composing);
  composing = compose::plain{};
  screen.line.show_context(std::nullopt);
  screen.line.clear();
  this->keep_draft(to, std::string());
}

void app::apply(const request::switch_account& one) {
  auto& screen = root().main();
  screen.current = mux::account_id{mux::ui::protocol_of(one.address), one.address};
  screen.chosen.reset();
  root().close_drawer();
  this->refresh();
}

void app::apply(const request::pop_panel&) {
  pending_login.reset();
  root().back_panel();
  this->refresh();
}

void app::apply(const request::settings_home&) {
  if (auto* up = root().settings_up())
    up->show_home();
}

void app::apply(const request::settings_animations&) {
  if (auto* up = root().settings_up())
    up->show_animations();
}

void app::set_motion(std::string level) {
  skiff::paint::motionLevel() = motion_of(level);
  root().show_motion(level);
  motion = std::move(level);
  (void)this->write();
}

}  // namespace mux::app
