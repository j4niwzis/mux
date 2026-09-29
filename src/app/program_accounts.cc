// SPDX-License-Identifier: AGPL-3.0-only
// The program: accounts, their pages, privacy and proxies.
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
import mux.app.screens;
import mux.app.words;

namespace mux::app {

// A person's info: a box in the middle of the window, as tdesktop's.
void app::apply(const request::open_member_info& one) {
  const auto& chosen = root().main().chosen;
  if (!chosen)
    return;
  const mux::conversation* in = model->find(*chosen);
  root().open_person(chosen->account, one.id, mux::ui::person_of(in, *model, chosen->account, one.id));
}

void app::apply(const request::close_person_info&) { root().close_person(); }

// The input's emoji panel: opened over the chat above its button, or closed.
void app::apply(const request::toggle_emoji&) {
  if (root().emoji_open()) {
    root().close_emoji();
    return;
  }
  const auto at = root().main().line.parts.input.parts.emoji.bounds();
  root().open_emoji(at.fRight, at.fTop - 6.0f);
}
void app::apply(const request::close_emoji&) { root().close_emoji(); }
// An emoji picked: into what is written, where the caret is; the input keeps
// the keys.
void app::apply(const request::insert_emoji& one) {
  root().main().line.parts.input.parts.field.insertText(one.text);
  scene.focus(root().main().line.field);
}

void app::apply(const request::not_implemented& one) { root().show_notice(one.what); }

void app::apply(const request::close_notice&) { root().close_notice(); }

void app::apply(const request::resize_info& one) { root().main().resize_info(one.x); }

void app::apply(const request::choose_new_proxy& one) {
  if (auto* up = root().open_panel())
    std::visit(
        [&](accounts& panel) {
          if (auto* pane = panel.adding())
            pane->set_proxy(one.index);
        },
        *up);
}

void app::apply(const request::toggle_mute&) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  if (!muted.erase(*screen.chosen))
    muted.insert(*screen.chosen);
  (void)this->write();
  this->refresh();
}

void app::apply(const request::close_account_pages&) {
  if (auto* up = root().open_panel())
    std::visit([](accounts& panel) { panel.close_pages(); }, *up);
}

void app::apply(const request::accounts_back&) {
  auto* up = root().open_panel();
  if (!up)
    return;
  std::visit(
      [this](accounts& panel) {
        if (panel.pages_open())
          panel.close_pages();
        else
          this->apply(request::pop_panel{});
      },
      *up);
}

void app::apply(const request::account_page& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    panel.show_page(one.page, account, *model, proxies);
  });
}

void app::apply(const request::flip_account_receipts&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::read_receipts_in(account);
    kept = !kept.value_or(true);
    if (auto* page = panel.privacy())
      page->show(*kept, mux::config::send_typing_of(account));
    (void)this->write();
  });
}

void app::apply(const request::flip_account_typing&) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::send_typing_in(account);
    kept = !kept.value_or(true);
    if (auto* page = panel.privacy())
      page->show(mux::config::read_receipts_of(account), *kept);
    (void)this->write();
  });
}

void app::apply(const request::proxy_kind& one) {
  if (auto* up = root().settings_up())
    if (auto* editor = up->editor())
      editor->set_kind(one.kind);
}

void app::apply(const request::choose_account_proxy& one) {
  this->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
    auto& kept = mux::config::proxy_in(account);
    if (one.index < 0 || static_cast<std::size_t>(one.index) >= proxies.size())
      kept.reset();
    else
      kept = proxies[static_cast<std::size_t>(one.index)].name;
    (void)this->write();
    this->reconnect(account);
    panel.show_page(2, account, *model, proxies);
  });
}

void app::reconnect(const mux::config::account_t& account) {
  if (!mux::config::enabled_of(account) || ask.demo)
    return;
  net->remove(mux::config::address_of(account));
  net->add(account, proxies);
}

void app::reconnect_through(const std::string& name) {
  for (const auto& one : saved)
    if (mux::config::proxy_of(one) == name)
      this->reconnect(one);
}

}  // namespace mux::app
