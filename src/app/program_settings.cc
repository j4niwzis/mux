// SPDX-License-Identifier: AGPL-3.0-only
// The program: themes, appearance, rendering and storage.
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

void app::rebuild_in_theme() {
  auto& before = root().main();
  const auto chosen = before.chosen;
  const auto current = before.current;
  const float side_width = before.side_width;
  const float info_width = before.info_width;
  const bool info_open = before.info_open;
  const float settings_at = root().settings_up() ? root().settings_up()->offset() : 0.0f;
  mux::ui::use_theme(theme, accent);
  pending_login.reset();
  drawer_waits = false;
  root().rebuild();
  auto& after = root().main();
  after.chosen = chosen;
  after.current = current;
  after.side_width = side_width;
  after.info_width = info_width;
  after.info_open = info_open;
  this->refresh();
  root().open_settings(motion.value_or("full"));
  if (auto* up = root().settings_up()) {
    up->show_appearance(theme, accent);
    up->keep_offset(settings_at);
  }
}

void app::apply(const request::manage_proxies&) {
  root().open_settings(motion.value_or("full"));
  if (auto* up = root().settings_up())
    up->show_proxies(proxies, false);
}

void app::apply(const request::settings_proxies&) {
  if (auto* up = root().settings_up())
    up->show_proxies(proxies);
}

void app::apply(const request::add_proxy&) {
  if (auto* up = root().settings_up())
    up->show_proxy(std::nullopt, -1);
}

void app::apply(const request::edit_proxy& one) {
  if (auto* up = root().settings_up(); up && one.index >= 0 && static_cast<std::size_t>(one.index) < proxies.size())
    up->show_proxy(proxies[static_cast<std::size_t>(one.index)], one.index);
}

void app::apply(const request::save_proxy_profile&) {
  auto* up = root().settings_up();
  auto* editor = up ? up->editor() : nullptr;
  if (!editor)
    return;
  auto typed = editor->proxy();
  if (!typed) {
    editor->say(typed.error(), true);
    return;
  }
  for (std::size_t i = 0; i < proxies.size(); ++i)
    if (proxies[i].name == typed->name && static_cast<int>(i) != editor->index) {
      editor->say("There is a proxy of that name already.", true);
      return;
    }
  const std::string name = typed->name;
  if (editor->index < 0) {
    proxies.push_back(std::move(*typed));
  } else {
    auto& kept = proxies[static_cast<std::size_t>(editor->index)];
    for (auto& one : saved)
      if (auto& uses = mux::config::proxy_in(one); uses == kept.name)
        uses = name;
    kept = std::move(*typed);
  }
  if (auto failed = this->write()) {
    editor->say(*failed, true);
    return;
  }
  this->reconnect_through(name);
  up->show_proxies(proxies);
  this->refresh();  // the new account's row of proxies, where it is being added
}

void app::apply(const request::delete_proxy_profile&) {
  auto* up = root().settings_up();
  auto* editor = up ? up->editor() : nullptr;
  if (!editor || editor->index < 0 || static_cast<std::size_t>(editor->index) >= proxies.size())
    return;
  const std::string name = proxies[static_cast<std::size_t>(editor->index)].name;
  // In use: not deleted. The accounts that go through it would otherwise
  // connect straight to their servers, this machine's address shown to
  // them, without a word.
  const auto users = saved | std::views::filter([&](const auto& one) { return mux::config::proxy_of(one) == name; }) |
                     std::views::transform([](const auto& one) { return mux::config::address_of(one); }) |
                     std::ranges::to<std::vector<std::string>>();
  if (!users.empty()) {
    editor->say(std::format("In use by {}: choose another proxy for them, or none, first.",
                            users | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>()),
                true);
    return;
  }
  proxies.erase(proxies.begin() + editor->index);
  (void)this->write();
  up->show_proxies(proxies);
}

}  // namespace mux::app
