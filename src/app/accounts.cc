// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.accounts: the accounts page -- the accounts listed, one's pages,
// a new one added through its protocol's form and waited for as it logs
// in, one edited, turned on or off, or removed. A part of the program: it
// owns the login being waited for and the proxy a new account is to go
// through, and reaches the settings it is given and the rest through the
// services.
export module mux.app.accounts;

import std;
import splice;
import mux.core;
import mux.config;
import mux.protocols;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class accounts_part {
 public:
  using accounts = mux::ui::accounts_panel<actions>;
  accounts_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  accounts_part(const accounts_part&) = delete;
  accounts_part& operator=(const accounts_part&) = delete;

  // The login being waited for, let go: the page left.
  void forget_login() { pending_login_.reset(); }

  // The accounts page. From the drawer, it comes in over it, and the drawer
  // goes once the page is in: not two things moving at once.
  accounts& show_accounts() {
    if (s_->root().drawer_open())
      s_->drawer_waits = true;
    s_->root().close_settings();
    pending_login_.reset();
    auto& panel = s_->root().open<accounts>();
    if (k_->config_error)
      panel.say(*k_->config_error);
    s_->refresh_due = true;
    return panel;
  }
  // The accounts, with this one's settings up beside them.
  accounts& show_account(const std::string& address) {
    auto& panel = this->show_accounts();
    if (const auto found = k_->find(address); found != k_->saved.end()) {
      panel.select(*found, *s_->model);
      panel.show(k_->saved, *s_->model);
    }
    return panel;
  }
  // Adding an account: beside the list, on the accounts page.
  void show_adding() {
    auto& panel = this->show_accounts();
    panel.proxies = k_->proxies;
    panel.show_adding();
    s_->refresh_due = true;
  }
  // The page brought up to date with the model and the settings. Whether
  // the account being added is in now -- the chats to be shown.
  [[nodiscard]] bool bring_up_to_date(accounts& panel) {
    panel.proxies = k_->proxies;
    panel.show(k_->saved, *s_->model);
    auto* pane = panel.adding();
    return pane && spl::visit([this](auto& form) { return this->watch_login(form); }, pane->parts.form);
  }

  void apply(const request::open_accounts&) { (void)this->show_accounts(); }
  void apply(const request::open_new_account&) { this->show_adding(); }
  void apply(const request::show_account& one) { (void)this->show_account(one.address); }
  // A protocol chosen for the account being added: its form.
  void apply(const request::add_account_of& one) {
    auto* up = s_->root().open_panel();
    if (!up)
      return;
    pending_login_.reset();
    spl::visit(
        [&](accounts& panel) {
          if (auto* pane = panel.adding())
            pane->show(one.speaks);
        },
        *up);
  }
  void apply(const request::select_account& one) {
    auto* up = s_->root().open_panel();
    if (!up)
      return;
    spl::visit(
        [&](accounts& panel) {
          if (const auto found = k_->find(one.address); found != k_->saved.end()) {
            pending_login_.reset();
            panel.select(*found, *s_->model);
            panel.show(k_->saved, *s_->model);
          }
        },
        *up);
  }
  // A form's own buttons -- XMPP's Advanced, its PLAIN switch -- asked of the
  // form up, where it has them.
  void apply(const request::toggle_advanced&) {
    if (auto* up = this->form_up())
      spl::visit([](auto& form) {
        mux::app::ask_if_able([](auto& f) -> decltype(void(f.show_advanced(!f.advanced))) { f.show_advanced(!f.advanced); }, form);
      }, *up);
  }
  void apply(const request::toggle_plain&) {
    if (auto* up = this->form_up())
      spl::visit([](auto& form) {
        mux::app::ask_if_able([](auto& f) -> decltype(void(f.flip_plain())) { f.flip_plain(); }, form);
      }, *up);
  }
  // The form sent: an account edited, or a new one added.
  void apply(const request::submit_login&) {
    auto* up = s_->root().open_panel();
    if (!up)
      return;
    spl::visit(
        [this](accounts& panel) {
          if (auto* editor = panel.editor())
            spl::visit([this](auto& form) { this->edit(form); }, editor->parts.form);
          else if (auto* pane = panel.adding()) {
            new_proxy_ = pane->proxy;
            spl::visit([this](auto& form) { this->add(form); }, pane->parts.form);
          }
        },
        *up);
  }
  // An account turned on -- started -- or off -- stopped.
  void apply(const request::flip_enabled& one) {
    const auto found = k_->find(one.address);
    if (found == k_->saved.end())
      return;
    bool& enabled = mux::config::enabled_of(*found);
    enabled = !enabled;
    if (enabled)
      s_->net->add(*found, k_->proxies);
    else
      s_->net->remove(one.address);
    this->save();
    s_->refresh_due = true;
  }
  void apply(const request::remove_account& one) {
    if (std::erase_if(k_->saved, [&](const auto& each) { return mux::config::address_of(each) == one.address; }) == 0)
      return;
    s_->net->remove(one.address);
    this->save();
    s_->refresh_due = true;
  }
  // The proxy the account being added is to go through.
  void apply(const request::choose_new_proxy& one) {
    if (auto* up = s_->root().open_panel())
      spl::visit(
          [&](accounts& panel) {
            if (auto* pane = panel.adding())
              pane->set_proxy(one.index);
          },
          *up);
  }
  void apply(const request::close_account_pages&) {
    if (auto* up = s_->root().open_panel())
      spl::visit([](accounts& panel) { panel.close_pages(); }, *up);
  }
  // A page of the chosen account: one that wants something of the server
  // asks for it as it opens.
  void apply(const request::account_page& one) {
    s_->with_chosen_account([&](accounts& panel, mux::config::account_t& account) {
      panel.show_page(one.page, account, *s_->model, k_->proxies, k_->theme);
    });
  }

 private:
  // The panel that is up, if one is, and the form in it, if there is one.
  [[nodiscard]] mux::ui::account_form<actions>* form_up() {
    auto* up = s_->root().open_panel();
    if (!up)
      return nullptr;
    return spl::visit([](auto& panel) { return panel.form(); }, *up);
  }
  // A new account waiting to log in: online, it is in; failed, it is said.
  template <class Form>
  [[nodiscard]] bool watch_login(Form& form) {
    if (!pending_login_)
      return false;
    const auto found = s_->model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login_), *pending_login_});
    if (found == s_->model->accounts().end())
      return false;
    return spl::visit(spl::overloaded{[&](const mux::connection::online&) {
                                              pending_login_.reset();
                                              return true;
                                            },
                                            [&](const mux::connection::failed& why) {
                                              form.say(why.error.empty() ? "The server said no." : why.error, true);
                                              pending_login_.reset();
                                              return false;
                                            },
                                            [&](const auto&) {
                                              form.say("Connecting\u2026", false);
                                              return false;
                                            }},
                         found->second.state);
  }
  // A new account: saved, and started; the page waits to hear how it went.
  template <class Form>
  void add(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{.own = mux::config::kept_t{std::move(*typed)}};
    mux::config::proxy_in(account) = std::exchange(new_proxy_, std::nullopt);
    const std::string address = mux::config::address_of(account);
    if (k_->find(address) != k_->saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    k_->saved.push_back(account);
    if (auto failed = k_->write()) {
      form.say(*failed, true);
      return;
    }
    s_->net->add(account, k_->proxies);
    pending_login_ = address;
    form.say("Connecting\u2026", false);
  }
  // An account's settings changed: the old one stops, and the new one, on
  // or off as the old one was, takes its place in the list.
  template <class Form>
  void edit(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{.own = mux::config::kept_t{std::move(*typed)}};
    const std::string address = mux::config::address_of(account);
    const std::string was = form.editing.value_or(address);
    const auto old = k_->find(was);
    if (old == k_->saved.end())
      return;
    if (address != was && k_->find(address) != k_->saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    // What the form does not show is kept: every setting every account has
    // -- on or off, the proxy, receipts, its colour and look.
    account.shared = old->shared;
    // And what its protocol keeps through an edit (a Matrix session: the
    // device it has, not a new one at every Save).
    spl::visit([](auto& now, const auto& before) {
                    using mux::proto::kept_defaults::carry_over;
                    carry_over(now, before);
                  },
                  account.own, std::as_const(old->own));
    // Nothing changed: saved as it is, and the connection left alone.
    const bool same = account == *old;
    *old = account;
    const auto failed = k_->write();
    if (!same) {
      s_->net->remove(was);
      if (mux::config::enabled_of(account))
        s_->net->add(account, k_->proxies);
    }
    // The form is made again from what was saved: `form` is gone after this.
    auto& panel = this->show_account(address);
    if (auto* editor = panel.editor())
      editor->say(failed ? *failed : std::string("Saved."), failed.has_value());
  }
  // Written, and what went wrong said on the accounts page.
  void save() {
    if (auto failed = k_->write())
      if (auto* up = s_->root().open_panel())
        spl::visit([&](accounts& panel) { panel.say(*failed); }, *up);
  }

  services* s_;
  kept_settings* k_;
  // The account being added that a login is waiting to hear about, and the
  // proxy it is to go through.
  std::optional<std::string> pending_login_;
  std::optional<std::string> new_proxy_;
};

}  // namespace mux::app
