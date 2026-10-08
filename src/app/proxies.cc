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
    s_->net->add(account, k_->proxies());
  }
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name) {
    for (const auto& one : k_->accounts().values())
      if (mux::config::proxy_of(one) == name)
        this->reconnect(one);
  }

  // Settings opened on the proxies, from an account's page.
  void apply(const request::manage_proxies&) {
    mux::ui::show(*s_->showing, std::optional(mux::ui::settings_facts{mux::ui::settings_page::proxies{k_->proxies(), false}}));
  }
  void apply(const request::settings_proxies&) {
    if (auto* up = s_->root().settings_up())
      s_->settings_page(mux::ui::settings_page::proxies{k_->proxies()});
  }
  void apply(const request::add_proxy&) {
    if (s_->root().settings_up()) {
      mux::ui::show(*s_->showing, mux::ui::proxy_notice{});
      s_->settings_page(mux::ui::settings_page::proxy{std::nullopt, -1});
    }
  }
  void apply(const request::edit_proxy& one) {
    if (s_->root().settings_up() && one.index >= 0 && static_cast<std::size_t>(one.index) < k_->proxies().size()) {
      mux::ui::show(*s_->showing, mux::ui::proxy_notice{});
      s_->settings_page(mux::ui::settings_page::proxy{k_->proxies()[static_cast<std::size_t>(one.index)], one.index});
    }
  }
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it, which are connected again through it.
  void apply(const request::save_proxy_profile& request) {
    if (!s_->root().settings_up()) return;
    auto typed = request.profile;
    if (!typed) {
      mux::ui::show(*s_->showing, mux::ui::proxy_notice{typed.error()});
      return;
    }
    if (request.index >= 0 && static_cast<std::size_t>(request.index) >= k_->proxies().size()) return;
    const bool taken = std::ranges::any_of(std::views::enumerate(k_->proxies()), [&](const auto& each) {
      const auto& [at, one] = each;
      return one.name == typed->name && at != request.index;
    });
    if (taken) {
      mux::ui::show(*s_->showing, mux::ui::proxy_notice{"There is a proxy of that name already."});
      return;
    }
    const std::string name = typed->name;
    if (request.index < 0) {
      k_->change_part<std::vector<mux::config::proxy_settings>>([&](auto& all) { all.push_back(std::move(*typed)); });
    } else {
      const auto& kept = k_->proxies()[static_cast<std::size_t>(request.index)];
      // The accounts going through it, through it under its new name.
      const auto users = std::ranges::to<std::vector<std::string>>(
          std::views::filter(k_->accounts().keys(), [&](const std::string& address) {
            return mux::config::proxy_of(*k_->settings_of(address)) == kept.name;
          }));
      for (const std::string& address : users)
        k_->change_account(address, [&](mux::config::account_t& one) { mux::config::proxy_in(one) = name; });
      k_->change_part<std::vector<mux::config::proxy_settings>>(
          [&](auto& all) { all[static_cast<std::size_t>(request.index)] = std::move(*typed); });
    }
    if (auto failed = k_->write()) {
      mux::ui::show(*s_->showing, mux::ui::proxy_notice{*failed});
      return;
    }
    this->reconnect_through(name);
    s_->settings_page(mux::ui::settings_page::proxies{k_->proxies()});
    s_->refresh_due = true;  // the new account's row of proxies, where it is being added
  }
  // A profile deleted -- not while an account goes through it: those would
  // otherwise connect straight to their servers, this machine's address
  // shown to them, without a word.
  void apply(const request::delete_proxy_profile& request) {
    if (!s_->root().settings_up() || request.index < 0 || static_cast<std::size_t>(request.index) >= k_->proxies().size())
      return;
    const std::string name = k_->proxies()[static_cast<std::size_t>(request.index)].name;
    const auto users = std::ranges::to<std::vector<std::string>>(std::views::transform(std::views::filter(k_->accounts().values(), [&](const auto& one) { return mux::config::proxy_of(one) == name; }), [](const auto& one) { return mux::config::address_of(one); }));
    if (!users.empty()) {
      mux::ui::show(*s_->showing, mux::ui::proxy_notice{std::format("In use by {}: choose another proxy for them, or none, first.",
                              std::ranges::to<std::string>(std::views::join_with(users, std::string_view(", "))))});
      return;
    }
    k_->change_part<std::vector<mux::config::proxy_settings>>([&](auto& all) { all.erase(all.begin() + request.index); });
    (void)k_->write();
    s_->settings_page(mux::ui::settings_page::proxies{k_->proxies()});
  }

 private:
  services* s_;
  kept_settings* k_;
};

}  // namespace mux::app
