// SPDX-License-Identifier: AGPL-3.0-only
// The program: files sent, pictures opened and saved, downloads.
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

void app::apply(const request::reply_to& one) {
  menu_target.id = one.id;
  menu_target.text = one.text;
  this->apply(request::menu_reply{});
}

void app::apply(const request::menu_reply&) {
  root().close_menu();
  // As tdesktop's: "Reply to <name>" over a line of the message -- its
  // text, or what it carries where it has none.
  std::string title = "Reply";
  std::string line = menu_target.text;
  if (const auto& chosen = root().main().chosen)
    if (const mux::conversation* chat = model->find(*chosen)) {
      const auto said = std::ranges::find(chat->timeline, menu_target.id, &mux::message::id);
      if (said != chat->timeline.end()) {
        title = "Reply to " + mux::ui::sender_name(*chat, said->sender);
        if (line.empty() && said->attachment)
          line = mux::is_picture(said->attachment->kind) ? std::string("Photo") : said->attachment->name;
      }
    }
  std::ranges::replace(line, '\n', ' ');
  outbox.answer(menu_target.id, std::move(title), std::move(line));
}

void app::apply(const request::menu_edit&) {
  root().close_menu();
  outbox.edit(menu_target.id, menu_target.text);
}

void app::apply(const request::menu_copy&) {
  root().close_menu();
  skiff::scene::setClipboardText(menu_target.copied.empty() ? menu_target.text : menu_target.copied);
}

void app::apply(const request::react& one) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (!chat)
    return;
  const auto said = std::ranges::find(chat->timeline, one.id, &mux::message::id);
  if (said == chat->timeline.end())
    return;
  const std::string& me = chosen->account.address;
  const auto who = said->reactions.find(one.key);
  const bool on = who == said->reactions.end() || !who->second.contains(me);
  model->apply(mux::change_t{mux::change::reaction_changed{*chosen, one.id, one.key, me, on}});
  if (!ask.demo)
    net->react(*chosen, one.id, one.key, on);
  this->refresh();
}

void app::apply(const request::menu_react& one) {
  root().close_menu();
  this->apply(request::react{menu_target.id, one.key});
}

void app::apply(const request::menu_copy_link&) {
  root().close_menu();
  skiff::scene::setClipboardText(menu_target.link);
}

void app::apply(const request::menu_save&) {
  root().close_menu();
  if (menu_target.media)
    pictures.save(*menu_target.media, menu_target.media_name.empty() ? std::string("image") : menu_target.media_name);
}

void app::apply(const request::menu_delete&) {
  root().close_menu();
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  if (ask.demo)
    box->push(mux::change_t{mux::change::message_redacted{*chosen, menu_target.id}});
  else
    net->remove_message(*chosen, menu_target.id);
}

void app::apply(const request::open_url& one) {
  if (const auto where = mux::logic::link_of(one.url)) {
    this->follow(*where);
    return;
  }
  mux::host::open_url(one.url);
}

}  // namespace mux::app
