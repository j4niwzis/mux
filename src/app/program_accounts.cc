// SPDX-License-Identifier: AGPL-3.0-only
// The program: accounts, their pages, privacy and proxies.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.logic.links;
import mux.protocols;
import mux.logic.room_events;
import mux.config;
import mux.net;
import mux.media;
import mux.platform.dialogs;
import mux.platform.push;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

// A person's info: a box in the middle of the window, as tdesktop's.
void app::apply(const request::open_member_info& one) {
  const auto chosen = shared.managed();
  if (!chosen)
    return;
  const mux::conversation* in = model->find(*chosen);
  root().open_person(chosen->account, one.id, mux::ui::person_of(in, *model, chosen->account, one.id));
  person_open_ = std::pair{*chosen, one.id};
  if (!shared.demo()) {
    net->ask_trust(chosen->account, one.id);
    net->ask_devices(chosen->account, one.id);
  }
}

void app::apply(const request::close_person_info&) {
  person_open_.reset();
  root().close_person();
}

// The input's emoji panel: opened over the chat above its button, or closed.
void app::apply(const request::toggle_emoji&) {
  if (root().emoji_open()) {
    root().close_emoji();
    return;
  }
  emoji_into_ = request::writing::chat{};
  const auto at = root().main().line.parts.input.parts.emoji.bounds();
  this->open_emoji_at(at.fRight, at.fTop);
}
// The thread's: the same panel, over its button, writing in its field.
void app::apply(const request::toggle_thread_emoji&) {
  if (root().emoji_open()) {
    root().close_emoji();
    return;
  }
  emoji_into_ = request::writing::thread{};
  const auto at = root().main().parts.threads.parts.line.parts.input.parts.emoji.bounds();
  this->open_emoji_at(at.fRight, at.fTop);
}
void app::open_emoji_at(float right, float top) {
  const auto chosen = shared.managed();
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  mux::ui::chat_emotes() = chat ? chat->emotes : std::vector<mux::emote>{};
  mux::ui::chat_stickers() = chat ? chat->stickers : std::vector<mux::emote>{};
  // Under a finger, in place of the on-screen keyboard, as Telegram's: the
  // field let go of, the keyboard goes down; tapping the field again closes
  // the panel and brings the keyboard back.
  if (by_touch)
    scene.clearFocus();
  root().open_emoji(right, top - 6.0f);
}
void app::apply(const request::close_emoji&) { root().close_emoji(); }

// Threads, as Element's panel: opened in place of the chat's info, the
// room's listed by the server as it opens; one opened, its answers fetched
// (and its root, where it is not held); an answer sent in the one open --
// falling back, for clients without threads, to its latest event.
void app::apply(const request::toggle_threads&) {
  auto& screen = root().main();
  if (screen.toggle_threads() && screen.chosen && !shared.demo())
    net->list_threads(*screen.chosen);
  this->refresh();
}
void app::apply(const request::open_thread& one) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  screen.open_thread(one.root);
  if (!shared.demo()) {
    net->load_thread(*screen.chosen, one.root);
    if (const mux::conversation* chat = model->find(*screen.chosen);
        chat && !chat->quoted.contains(one.root) &&
        std::ranges::find(chat->timeline, one.root, &mux::message::id) == chat->timeline.end())
      net->fetch_quoted(*screen.chosen, one.root);
  }
  this->refresh();
}
void app::apply(const request::close_thread&) {
  root().main().close_thread();
  this->refresh();
}
void app::apply(const request::send_in_thread& one) {
  const auto chosen = shared.managed();
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (!chat || shared.demo())
    return;
  std::string latest = one.root;
  if (const auto found = chat->threads.find(one.root); found != chat->threads.end() && !found->second.empty())
    latest = found->second.back().id;
  net->send_in_thread(*chosen, one.text, one.root, latest, one.reply_to);
}

void app::apply(const request::copy_text& one) {
  skiff::scene::setClipboardText(one.text);
  root().close_text_menu();
}

// The developer tools, for the chat being read.
void app::apply(const request::close_dialog&) { root().close_dialog(); }
// An emoji picked: into what is written, where the caret is; the input keeps
// the keys.
void app::apply(const request::insert_emoji& one) {
  auto& screen = root().main();
  // A custom emoji: its picture in the line, as the message will show it,
  // sent as its shortcode.
  const auto put = [&](auto& field) {
    if (one.picture.empty())
      field.insertText(one.text);
    else
      field.insertAtom("\u2003", one.picture, one.text, true);
    // Under a finger the panel stands where the keyboard would: focused,
    // the field brought the keyboard up over the panel at each emoji.
    if (!by_touch)
      scene.focus(field);
  };
  splice::visit(splice::overloaded{[&](request::writing::chat) { put(screen.line.field); },
                                   [&](request::writing::thread) { put(screen.parts.threads.parts.line.parts.input.parts.field); }},
                emoji_into_);
}

void app::apply(const request::not_implemented& one) { root().show_notice(one.what); }

void app::apply(const request::close_notice&) { root().close_notice(); }

void app::apply(const request::resize_info& one) { root().main().resize_info(one.x); }

void app::apply(const request::choose_new_proxy& one) {
  if (auto* up = root().open_panel())
    splice::visit(
        [&](accounts& panel) {
          if (auto* pane = panel.adding())
            pane->set_proxy(one.index);
        },
        *up);
}

void app::apply(const request::close_account_pages&) {
  if (auto* up = root().open_panel())
    splice::visit([](accounts& panel) { panel.close_pages(); }, *up);
}

void app::apply(const request::accounts_back&) {
  auto* up = root().open_panel();
  if (!up)
    return;
  splice::visit(
      [this](accounts& panel) {
        if (panel.step_back())
          return;
        if (panel.pages_open())
          panel.close_pages();
        else
          this->apply(request::pop_panel{});
      },
      *up);
}

void app::apply(const request::account_page& one) {
  shared.with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    // A page that wants something of the server asks for it as it opens.
    panel.show_page(one.page, account, *model, proxies, theme);
  });
}

// A tombstoned room's way on: the room it was upgraded to, as its
// protocol's link to it opens -- the chat where it is joined, its card where not.
void app::apply(const request::open_replacement&) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (const auto successor = chat ? mux::proto::successor_of(mux::ui::protocol_state_of(chosen->account), *chat) : std::nullopt)
    if (auto link = mux::proto::share_link(mux::ui::protocol_state_of(chosen->account), *successor))
      this->apply(request::open_url{std::move(*link)});
}

}  // namespace mux::app
