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
import mux.app.words;

namespace mux::app {

void app::apply(const request::open_member_info& one) {
  auto& screen = root().main();
  if (!screen.info_open)
    screen.toggle_info();
  screen.info.open_member(one.id);
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

void app::apply(const request::typing& one) {
  if (ask.demo)
    return;
  const auto& chosen = root().main().chosen;
  const auto now = std::chrono::steady_clock::now();
  const auto allowed = [&](const mux::conversation_id& in) {
    const auto account = this->find(in.account.address);
    return account != saved.end() && mux::config::send_typing_of(*account);
  };
  if (typing_in && (!one.on || typing_in != chosen)) {
    if (allowed(*typing_in))
      net->typing(*typing_in, false);
    typing_in.reset();
  }
  if (one.on && chosen && allowed(*chosen) &&
      (typing_in != chosen || now - typing_said > std::chrono::seconds(20))) {
    net->typing(*chosen, true);
    typing_in = chosen;
    typing_said = now;
  }
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

void app::apply(const request::settings_appearance&) {
  if (auto* up = root().settings_up())
    up->show_appearance(theme, accent);
}

void app::apply(const request::settings_files&) {
  if (auto* up = root().settings_up())
    up->show_files(sending);
}

void app::apply(const request::flip_strip_metadata&) {
  sending.strip_metadata = !sending.strip_metadata;
  (void)this->write();
}

void app::apply(const request::flip_rename_pictures&) {
  sending.rename = !sending.rename;
  (void)this->write();
}

void app::apply(const request::settings_storage&) {
  if (auto* up = root().settings_up())
    up->show_storage(limits);
}

void app::apply(const request::change_limit& one) {
  std::int64_t& value = mux::config::value_of(limits, one.which);
  const auto [low, high] = mux::config::bounds_of(one.which);
  value = std::clamp(one.more ? value * 2 : value / 2, low, high);
  this->apply_limits();
  model->trim(static_cast<std::size_t>(limits.messages_in_memory), root().main().chosen);
  (void)this->write();
  if (auto* up = root().settings_up())
    if (auto* page = up->storage())
      page->show(limits);
}

void app::apply(const request::clear_stored&) {
  std::error_code failed;
  std::filesystem::remove_all(mux::config::state_path("messages"), failed);
  std::filesystem::remove_all(mux::config::cache_path("avatars"), failed);
  mux::ui::avatar_images().clear();
  mux::ui::thumbnails().clear();
  mux::ui::whole_pictures().clear();
  avatars_fetched.clear();
  thumbnails_fetched.clear();
  wholes_fetched.clear();
  root().show_message("Storage", "The stored messages and pictures are cleared.");
  this->refresh();
}

void app::apply(const request::settings_rendering&) {
  if (auto* up = root().settings_up())
    up->show_rendering(renderer);
}

void app::apply(const request::set_theme& one) {
  theme = one.theme;
  (void)this->write();
  this->rebuild_in_theme();
}

void app::apply(const request::set_accent& one) {
  accent = one.accent;
  (void)this->write();
  this->rebuild_in_theme();
}

}  // namespace mux::app
