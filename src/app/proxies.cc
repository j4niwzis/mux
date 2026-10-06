// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proxies: the proxy profiles -- listed in Settings, added, edited,
// saved and deleted -- and the accounts that go through one, connected
// again when it changes. A part of the program: it owns no state of its
// own beyond the settings it is given, and reaches the rest through the
// services.
export module mux.app.proxies;

import std;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class proxies_part {
 public:
  proxies_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  proxies_part(const proxies_part&) = delete;
  proxies_part& operator=(const proxies_part&) = delete;

  // An account connected again, through the profile it names now -- where
  // it is on, and not in the demo.
  void reconnect(const mux::config::account_t& account) {
    if (!mux::config::enabled_of(account) || s_->demo())
      return;
    s_->net->remove(mux::config::address_of(account));
    s_->net->add(account, k_->proxies);
  }
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name) {
    for (const auto& one : k_->saved)
      if (mux::config::proxy_of(one) == name)
        this->reconnect(one);
  }

  // Settings opened on the proxies, from an account's page.
  void apply(const request::manage_proxies&) {
    s_->root().open_settings(k_->motion.value_or("full"));
    if (auto* up = s_->root().settings_up())
      up->show_proxies(k_->proxies, false);
  }
  void apply(const request::settings_proxies&) {
    if (auto* up = s_->root().settings_up())
      up->show_proxies(k_->proxies);
  }
  void apply(const request::add_proxy&) {
    if (auto* up = s_->root().settings_up())
      up->show_proxy(std::nullopt, -1);
  }
  void apply(const request::edit_proxy& one) {
    if (auto* up = s_->root().settings_up(); up && one.index >= 0 && static_cast<std::size_t>(one.index) < k_->proxies.size())
      up->show_proxy(k_->proxies[static_cast<std::size_t>(one.index)], one.index);
  }
  // The kind of proxy the editor shows.
  void apply(const request::proxy_kind& one) {
    if (auto* up = s_->root().settings_up())
      if (auto* editor = up->editor())
        editor->set_kind(one.kind);
  }
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it, which are connected again through it.
  void apply(const request::save_proxy_profile&) {
    auto* up = s_->root().settings_up();
    auto* editor = up ? up->editor() : nullptr;
    if (!editor)
      return;
    auto typed = editor->proxy();
    if (!typed) {
      editor->say(typed.error(), true);
      return;
    }
    const bool taken = std::ranges::any_of(std::views::enumerate(k_->proxies), [&](const auto& each) {
      const auto& [at, one] = each;
      return one.name == typed->name && at != editor->index;
    });
    if (taken) {
      editor->say("There is a proxy of that name already.", true);
      return;
    }
    const std::string name = typed->name;
    if (editor->index < 0) {
      k_->proxies.push_back(std::move(*typed));
    } else {
      auto& kept = k_->proxies[static_cast<std::size_t>(editor->index)];
      for (auto& one : k_->saved)
        if (auto& uses = mux::config::proxy_in(one); uses == kept.name)
          uses = name;
      kept = std::move(*typed);
    }
    if (auto failed = k_->write()) {
      editor->say(*failed, true);
      return;
    }
    this->reconnect_through(name);
    up->show_proxies(k_->proxies);
    s_->refresh_due = true;  // the new account's row of proxies, where it is being added
  }
  // A profile deleted -- not while an account goes through it: those would
  // otherwise connect straight to their servers, this machine's address
  // shown to them, without a word.
  void apply(const request::delete_proxy_profile&) {
    auto* up = s_->root().settings_up();
    auto* editor = up ? up->editor() : nullptr;
    if (!editor || editor->index < 0 || static_cast<std::size_t>(editor->index) >= k_->proxies.size())
      return;
    const std::string name = k_->proxies[static_cast<std::size_t>(editor->index)].name;
    const auto users = std::ranges::to<std::vector<std::string>>(std::views::transform(std::views::filter(k_->saved, [&](const auto& one) { return mux::config::proxy_of(one) == name; }), [](const auto& one) { return mux::config::address_of(one); }));
    if (!users.empty()) {
      editor->say(std::format("In use by {}: choose another proxy for them, or none, first.",
                              std::ranges::to<std::string>(std::views::join_with(users, std::string_view(", ")))),
                  true);
      return;
    }
    k_->proxies.erase(k_->proxies.begin() + editor->index);
    (void)k_->write();
    up->show_proxies(k_->proxies);
  }

 private:
  services* s_;
  kept_settings* k_;
};

}  // namespace mux::app
