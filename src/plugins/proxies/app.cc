// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.proxies.app -- The proxies' part of the program: the profiles
// listed in Settings, added, edited, saved and deleted; an account's chosen;
// and the accounts that go through one connected again when it changes.
export module mux.plugin.proxies.app;

import std;
import mux.composition;
import mux.config;
import mux.ui;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.plugin.proxies.types;

export namespace mux::plugins::proxies {

struct part {
  app::services* s_ = nullptr;

  // An account connected again, through the profile it names now -- where
  // it is on, and not in the demo.
  void reconnect(const config::account_t& account) {
    if (!config::enabled_of(account) || s_->demo())
      return;
    s_->net->remove(config::address_of(account));
    s_->net->add(account, s_->kept->proxies());
  }
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name) {
    for (const auto& one : s_->kept->accounts().values())
      if (config::proxy_of(one) == name)
        this->reconnect(one);
  }
  // What the window shows of them -- the profiles' names, which each account
  // goes through -- kept as the settings are, between events.
  void tick() {
    const auto& now = *s_->showing->look<shown>();
    shown next{.error = now.error};
    next.names = s_->kept->proxies() | std::views::transform(&config::proxy_settings::name) | std::ranges::to<std::vector>();
    next.labels = s_->kept->proxies() | std::views::transform([](const config::proxy_settings& one) {
                    return std::format("{} ({} {}:{})", one.name, config::label_of(one.kind), one.host, one.port);
                  }) | std::ranges::to<std::vector>();
    for (const auto& one : s_->kept->accounts().values())
      if (const auto name = config::proxy_of(one))
        next.through.insert_or_assign(config::address_of(one), *name);
    if (next != now)
      ui::show(*s_->showing, std::move(next));
  }
  void error(std::string text) {
    ui::change_shown<shown>(*s_->showing, [&](shown& now) { now.error = std::move(text); });
  }

  // Settings opened on the profiles, from an account's page or one being
  // added: no arrow back to Settings' home.
  void apply(const manage&) {
    ui::show(*s_->showing, std::optional(ui::settings_facts{list_page{s_->kept->proxies(), false}}));
  }
  void apply(const open_list&) {
    if (s_->settings_open())
      s_->settings_page(list_page{s_->kept->proxies()});
  }
  void apply(const add&) {
    if (!s_->settings_open())
      return;
    this->error({});
    s_->settings_page(editor_page{std::nullopt, -1});
  }
  void apply(const edit& one) {
    const auto& all = s_->kept->proxies();
    if (!s_->settings_open() || one.index < 0 || static_cast<std::size_t>(one.index) >= all.size())
      return;
    this->error({});
    s_->settings_page(editor_page{all[static_cast<std::size_t>(one.index)], one.index});
  }
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it, which are connected again through it.
  void apply(const save& request) {
    if (!s_->settings_open())
      return;
    auto typed = request.profile;
    if (!typed) {
      this->error(typed.error());
      return;
    }
    auto* kept = s_->kept;
    if (request.index >= 0 && static_cast<std::size_t>(request.index) >= kept->proxies().size())
      return;
    const bool taken = std::ranges::any_of(kept->proxies() | std::views::enumerate, [&](const auto& each) {
      const auto& [at, one] = each;
      return one.name == typed->name && at != request.index;
    });
    if (taken) {
      this->error("There is a proxy of that name already.");
      return;
    }
    const std::string name = typed->name;
    if (request.index < 0) {
      kept->change_part<std::vector<config::proxy_settings>>([&](auto& all) { all.push_back(std::move(*typed)); });
    } else {
      const std::string was = kept->proxies()[static_cast<std::size_t>(request.index)].name;
      // The accounts going through it, through it under its new name.
      const auto users = kept->accounts().keys() | std::views::filter([&](const std::string& address) {
            return config::proxy_of(*kept->settings_of(address)) == was;
          }) | std::ranges::to<std::vector<std::string>>();
      for (const std::string& address : users)
        kept->change_account(address, [&](config::account_t& one) { config::proxy_in(one) = name; });
      kept->change_part<std::vector<config::proxy_settings>>(
          [&](auto& all) { all[static_cast<std::size_t>(request.index)] = std::move(*typed); });
    }
    if (auto failed = kept->write()) {
      this->error(*failed);
      return;
    }
    this->reconnect_through(name);
    s_->settings_page(list_page{kept->proxies()});
    s_->refresh_due = true;  // the new account's row of proxies, where it is being added
  }
  // A profile deleted -- not while an account goes through it: those would
  // otherwise connect straight to their servers, this machine's address shown
  // to them, without a word.
  void apply(const remove& request) {
    auto* kept = s_->kept;
    if (!s_->settings_open() || request.index < 0 || static_cast<std::size_t>(request.index) >= kept->proxies().size())
      return;
    const std::string name = kept->proxies()[static_cast<std::size_t>(request.index)].name;
    const auto users = kept->accounts().values() | std::views::filter([&](const auto& one) { return config::proxy_of(one) == name; }) | std::views::transform([](const auto& one) { return config::address_of(one); }) | std::ranges::to<std::vector<std::string>>();
    if (!users.empty()) {
      this->error(std::format("In use by {}: choose another proxy for them, or none, first.",
                              users | std::views::join_with(std::string_view(", ")) | std::ranges::to<std::string>()));
      return;
    }
    kept->change_part<std::vector<config::proxy_settings>>([&](auto& all) { all.erase(all.begin() + request.index); });
    (void)kept->write();
    s_->settings_page(list_page{kept->proxies()});
  }
  // An account's profile chosen on its page -- none, or one by its place --
  // and the account connected again through it.
  void apply(const choose& one) {
    auto* kept = s_->kept;
    if (kept->settings_of(one.account) == nullptr)
      return;
    kept->change_account(one.account, [&](config::account_t& account) {
      auto& chosen = config::proxy_in(account);
      if (one.index < 0 || static_cast<std::size_t>(one.index) >= kept->proxies().size())
        chosen.reset();
      else
        chosen = kept->proxies()[static_cast<std::size_t>(one.index)].name;
    });
    (void)kept->write();
    if (const auto* account = kept->settings_of(one.account))
      this->reconnect(*account);
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::proxies
