// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.accounts: the accounts page -- the accounts listed, one's pages,
// a new one added through its protocol's form and waited for as it logs
// in, one edited, turned on or off, or removed. A part of the program: what
// the page shows is its to say (panel_facts, the chosen account's page and
// form facts, the adding pane's), and it owns the login being waited for;
// it reaches the settings it is given and the rest through the services.
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

namespace mux::app::accounts_detail {
// What a page asks for as it opens -- its sessions, the profile -- where it
// asks anything: given to `asked`, in its own type.
template <class Page, class Asked>
  requires requires(Page page, const mux::account_id& by) { asked_as_it_opens(page, by); }
void ask_opening(Page page, const mux::account_id& by, const Asked& asked) {
  asked(asked_as_it_opens(page, by));
}
template <class Page, class Asked>
void ask_opening(Page, const mux::account_id&, const Asked&) {}
// What a protocol keeps beside the form's draft, put in the draft sent:
// XMPP's answers to what the server asked. Nothing, for the rest.
template <class Draft, class Part>
  requires requires(Draft& draft, const Part& part) { with_form_part(draft, part); }
void merge_part(Draft& draft, const Part& part) {
  with_form_part(draft, part);
}
template <class Draft, class Part>
void merge_part(Draft&, const Part&) {}
}  // namespace mux::app::accounts_detail

export namespace mux::app {

class accounts_part {
 public:
  accounts_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  accounts_part(const accounts_part&) = delete;
  accounts_part& operator=(const accounts_part&) = delete;

  // The login being waited for, let go: the page left.
  void forget_login() { pending_login_.reset(); }

  // The accounts page, with what is beside its list. From the drawer, it
  // comes in over it, and the drawer goes once the page is in: not two
  // things moving at once.
  void show_accounts(mux::ui::panel_detail_t detail = mux::ui::panel_detail::none{}) {
    // The drawer pushed back: it waits under the page coming in, and goes
    // as the page covers it (the slide-over tells it).
    if (s_->showing->look<mux::ui::drawer_shown>()->out)
      mux::ui::show(*s_->showing, mux::ui::drawer_shown{false});
    mux::ui::show<mux::ui::settings_facts>(*s_->showing, std::nullopt);
    pending_login_.reset();
    mux::ui::panel_facts facts{.note = k_->config_error, .theme = k_->appearance().theme};
    facts.narrow = this->narrow();
    mux::ui::show(*s_->showing, std::optional(std::move(facts)));
    this->show_detail(std::move(detail));
    s_->refresh_due = true;
  }
  // The accounts, with this one's settings up beside them.
  void show_account(const std::string& address) { this->show_accounts(mux::ui::panel_detail::account{address}); }
  // Adding an account: beside the list, on the accounts page.
  void show_adding() { this->show_accounts(mux::ui::panel_detail::adding{}); }
  // What a protocol keeps beside an account's form, told by the network --
  // XMPP's: what its server asks to register -- shown in that account's
  // form, with what the form says of it.
  template <class Part>
  void form_part_told(const std::string& address, Part part, std::string said) {
    if (this->chosen() != address)
      this->show_account(address);
    this->change_form([&](mux::ui::account_form_facts& now) {
      now.part = mux::ui::form_part_t(std::move(part));
      now.said = {std::move(said), false};
    });
  }

  // The page brought up to date with the model and the settings: the
  // accounts listed as they are, its width, the proxies the adding pane
  // lists; and the account being added, waited for. Whether it is in now
  // -- the chats to be shown.
  [[nodiscard]] bool bring_up_to_date() {
    const auto& open = *s_->showing->look<std::optional<mux::ui::panel_facts>>();
    if (!open)
      return false;
    auto rows = mux::ui::account_rows(k_->accounts().values(), *s_->model, this->chosen());
    const bool narrow = this->narrow();
    if (rows != open->rows || narrow != open->narrow || open->theme != k_->appearance().theme)
      this->change_panel([&](mux::ui::panel_facts& now) {
        now.rows = std::move(rows);
        now.narrow = narrow;
        now.theme = k_->appearance().theme;
      });
    // Nothing to show beside the list, and no account at all: adding one.
    if (std::ranges::empty(k_->accounts().values()) && this->detail_is<mux::ui::panel_detail::none>())
      this->show_detail(mux::ui::panel_detail::adding{});
    if (const auto& adding = *s_->showing->look<mux::ui::add_account_shown>(); adding.proxies != this->proxy_names())
      mux::ui::show(*s_->showing, mux::ui::with_proxies(adding, this->proxy_names()));
    return this->watch_login();
  }

  void apply(const request::open_accounts&) { this->show_accounts(); }
  void apply(const request::open_new_account&) { this->show_detail(mux::ui::panel_detail::adding{}); }
  void apply(const request::show_account& one) { this->show_account(one.address); }
  // A protocol chosen for the account being added: its form, blank.
  void apply(const request::add_account_of& one) {
    pending_login_.reset();
    auto adding = *s_->showing->look<mux::ui::add_account_shown>();
    adding.speaks = one.speaks;
    mux::ui::show(*s_->showing, std::move(adding));
    this->open_form(mux::ui::blank_draft(one.speaks), one.speaks, std::nullopt);
    s_->refresh_due = true;
  }
  // An account chosen from the list: its pages, its connection beside them
  // -- narrow, its pages first, not the first of them.
  void apply(const request::select_account& one) {
    if (k_->settings_of(one.address) == nullptr)
      return;
    pending_login_.reset();
    this->show_detail(mux::ui::panel_detail::account{one.address});
    this->change_panel([](mux::ui::panel_facts& now) { now.detail_up = false; });
  }
  // A page of the chosen account: its facts made afresh; one that wants
  // something of the server asks for it as it opens.
  template <class Asked>
  void apply(const request::account_page& one, const Asked& asked) {
    const auto address = this->chosen();
    if (!address)
      return;
    this->show_detail(mux::ui::panel_detail::account{*address, one.page});
    const mux::account_id by{mux::ui::protocol_of(*address), *address};
    spl::visit([&](auto page) { accounts_detail::ask_opening(page, by, asked); }, one.page);
  }
  // Back: narrow, from a page to the account's pages; from an account to
  // the list; from the list to the chats.
  void apply(const request::accounts_back&) {
    const auto& open = *s_->showing->look<std::optional<mux::ui::panel_facts>>();
    if (!open)
      return;
    if (open->narrow && open->detail_up)
      this->change_panel([](mux::ui::panel_facts& now) {
        now.detail_up = false;
        now.toward = -1.0f;
      });
    else if (!this->detail_is<mux::ui::panel_detail::none>())
      this->show_detail(mux::ui::panel_detail::none{});
    else
      this->close();
  }
  void apply(const request::close_account_pages&) { this->show_detail(mux::ui::panel_detail::none{}); }
  // The form sent: an account edited, or a new one added -- from its draft,
  // what its protocol keeps beside it put in, made settings by its
  // protocol (account_from), or what is wrong with it said.
  void apply(const request::submit_account& one) {
    const auto& open = *s_->showing->look<std::optional<mux::ui::account_form_facts>>();
    if (!open)
      return;
    auto draft = one.draft;
    spl::visit([&](auto& typed, const auto& part) { accounts_detail::merge_part(typed, part); }, draft, open->part);
    auto made = spl::visit([](const auto& typed) -> std::expected<mux::config::kept_t, std::string> {
      auto kept = account_from(typed);
      if (!kept)
        return std::unexpected(kept.error());
      return mux::config::kept_t{std::move(*kept)};
    }, draft);
    if (!made) {
      this->say(made.error(), true);
      return;
    }
    mux::config::account_t account{.own = std::move(*made)};
    if (open->editing)
      this->edit(std::move(account), *open->editing);
    else
      this->add(std::move(account));
  }
  // An account turned on -- started -- or off -- stopped.
  void apply(const request::flip_enabled& one) {
    k_->change_account(one.address, [](mux::config::account_t& account) {
      bool& enabled = mux::config::enabled_of(account);
      enabled = !enabled;
    });
    const auto* found = k_->settings_of(one.address);
    if (found == nullptr)
      return;
    if (mux::config::enabled_of(*found))
      s_->net->add(*found, k_->proxies());
    else
      s_->net->remove(one.address);
    this->save();
    s_->refresh_due = true;
  }
  // An account removed: its pages, where they were up, gone with it.
  void apply(const request::remove_account& one) {
    if (!k_->remove_account(one.address))
      return;
    s_->net->remove(one.address);
    this->save();
    if (this->chosen() == one.address)
      this->show_detail(mux::ui::panel_detail::none{});
    s_->refresh_due = true;
  }
  // The proxy the account being added is to go through.
  void apply(const request::choose_new_proxy& one) {
    auto adding = mux::ui::with_proxies(*s_->showing->look<mux::ui::add_account_shown>(), this->proxy_names());
    if (!one.name || std::ranges::contains(adding.proxies, *one.name))
      adding.proxy = one.name;
    mux::ui::show(*s_->showing, std::move(adding));
  }

 private:
  static constexpr float kNarrow = 600.0f;
  [[nodiscard]] bool narrow() const { return s_->showing->look<mux::ui::window_shape>()->width < kNarrow; }
  // The proxy profiles by their names, as the adding pane lists them.
  [[nodiscard]] std::vector<std::string> proxy_names() const {
    return k_->proxies() | std::views::transform(&mux::config::proxy_settings::name) | std::ranges::to<std::vector>();
  }
  // The account chosen, where one is.
  [[nodiscard]] std::optional<std::string> chosen() const { return mux::ui::chosen_of(*s_->showing->look<std::optional<mux::ui::panel_facts>>()); }
  template <class Detail>
  [[nodiscard]] bool detail_is() const {
    const auto& open = *s_->showing->look<std::optional<mux::ui::panel_facts>>();
    return open && spl::visit(spl::overloaded{[](const Detail&) { return true; }, [](const auto&) { return false; }}, open->detail);
  }
  template <class Change>
  void change_panel(Change change) {
    auto now = *s_->showing->look<std::optional<mux::ui::panel_facts>>();
    if (!now)
      return;
    change(*now);
    mux::ui::show(*s_->showing, std::move(now));
  }
  template <class Change>
  void change_form(Change change) {
    auto now = *s_->showing->look<std::optional<mux::ui::account_form_facts>>();
    if (!now)
      return;
    change(*now);
    mux::ui::show(*s_->showing, std::move(now));
  }
  // What the form says: what went wrong, or what is happening.
  void say(std::string text, bool error) {
    this->change_form([&](mux::ui::account_form_facts& now) { now.said = {std::move(text), error}; });
  }
  // A form of this draft, made afresh.
  void open_form(mux::ui::account_draft_t draft, const mux::protocol_t& speaks, std::optional<std::string> editing) {
    mux::ui::show(*s_->showing, std::optional(mux::ui::form_opened(std::move(draft), speaks, std::move(editing), ++form_round_)));
  }
  // What is beside the list, and what it reads: an account's page and its
  // facts -- its connection, its form, made from what it keeps -- or the
  // pane adding one and its blank form; narrow, the page up in place of the
  // list where it is one chosen from the list.
  void show_detail(mux::ui::panel_detail_t detail) {
    spl::visit(spl::overloaded{
                   [&](mux::ui::panel_detail::none) {
                     mux::ui::show<mux::ui::account_page_facts>(*s_->showing, std::nullopt);
                     mux::ui::show<mux::ui::account_form_facts>(*s_->showing, std::nullopt);
                   },
                   [&](const mux::ui::panel_detail::account& one) {
                     const auto* found = k_->settings_of(one.address);
                     if (found == nullptr)
                       return;
                     const mux::account_id by{mux::ui::protocol_of(one.address), one.address};
                     auto facts = mux::ui::account_page_opened(*found, mux::ui::protocol_state_of(s_->ui, by), one.page, *s_->model);
                     facts.proxies = k_->proxies();
                     mux::ui::show(*s_->showing, std::optional(std::move(facts)));
                     spl::visit(spl::overloaded{[&](mux::ui::account_page::connection) {
                                                  this->open_form(mux::ui::draft_of_account(*found), by.speaks, one.address);
                                                },
                                                [&](const auto&) { mux::ui::show<mux::ui::account_form_facts>(*s_->showing, std::nullopt); }},
                                one.page);
                   },
                   [&](mux::ui::panel_detail::adding) {
                     mux::ui::show<mux::ui::account_page_facts>(*s_->showing, std::nullopt);
                     auto adding = mux::ui::with_proxies(*s_->showing->look<mux::ui::add_account_shown>(), this->proxy_names());
                     mux::ui::show(*s_->showing, adding);
                     this->open_form(mux::ui::blank_draft(adding.speaks), adding.speaks, std::nullopt);
                   }},
               detail);
    const bool up = spl::visit(spl::overloaded{[](mux::ui::panel_detail::none) { return false; }, [](const auto&) { return true; }}, detail);
    this->change_panel([&](mux::ui::panel_facts& now) {
      // Back to nothing chosen comes in from the left; anything else, into
      // something, from the right.
      now.toward = up ? 1.0f : -1.0f;
      now.detail = std::move(detail);
      now.detail_up = up;
      now.rows = mux::ui::account_rows(k_->accounts().values(), *s_->model, mux::ui::chosen_of(std::optional(now)));
    });
  }
  // The page gone: back to the chats.
  void close() {
    pending_login_.reset();
    mux::ui::show<mux::ui::panel_facts>(*s_->showing, std::nullopt);
    mux::ui::show<mux::ui::account_page_facts>(*s_->showing, std::nullopt);
    mux::ui::show<mux::ui::account_form_facts>(*s_->showing, std::nullopt);
    s_->refresh_due = true;
  }
  // A new account waiting to log in: online, it is in; failed, it is said.
  [[nodiscard]] bool watch_login() {
    if (!pending_login_)
      return false;
    const mux::account* found = s_->model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login_), *pending_login_});
    if (found == nullptr)
      return false;
    return spl::visit(spl::overloaded{[&](const mux::connection::online&) {
                                        pending_login_.reset();
                                        return true;
                                      },
                                      [&](const mux::connection::failed& why) {
                                        this->say(why.error.empty() ? "The server said no." : why.error, true);
                                        pending_login_.reset();
                                        return false;
                                      },
                                      [&](const auto&) {
                                        if (s_->showing->look<std::optional<mux::ui::account_form_facts>>()->value_or(mux::ui::account_form_facts{}).said.text !=
                                            "Connecting…")
                                          this->say("Connecting…", false);
                                        return false;
                                      }},
                      found->state);
  }
  // A new account: saved, and started -- through the proxy chosen for it;
  // the page waits to hear how it went.
  void add(mux::config::account_t account) {
    mux::config::proxy_in(account) = s_->showing->look<mux::ui::add_account_shown>()->proxy;
    const std::string address = mux::config::address_of(account);
    if (!k_->add_account(account)) {
      this->say("That account is already here.", true);
      return;
    }
    if (auto failed = k_->write()) {
      this->say(*failed, true);
      return;
    }
    s_->net->add(account, k_->proxies());
    pending_login_ = address;
    this->say("Connecting…", false);
  }
  // An account's settings changed -- the one the form was opened for, by
  // the address it was kept under: the old one stops, and the new one, on
  // or off as the old one was, takes its place in the list.
  void edit(mux::config::account_t account, const std::string& was) {
    const std::string address = mux::config::address_of(account);
    const auto* old = k_->settings_of(was);
    if (old == nullptr)
      return;
    if (address != was && k_->settings_of(address) != nullptr) {
      this->say("That account is already here.", true);
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
    k_->change_account(was, [&](mux::config::account_t& kept) { kept = account; });
    const auto failed = k_->write();
    if (!same) {
      s_->net->remove(was);
      if (mux::config::enabled_of(account))
        s_->net->add(account, k_->proxies());
    }
    // Its page made again from what was saved, saying how that went.
    this->show_detail(mux::ui::panel_detail::account{address});
    this->say(failed ? *failed : std::string("Saved."), failed.has_value());
  }
  // Written, and what went wrong said at the page's top.
  void save() {
    if (auto failed = k_->write())
      this->change_panel([&](mux::ui::panel_facts& now) { now.note = *failed; });
  }

  services* s_;
  kept_settings* k_;
  // The account being added that a login is waiting to hear about.
  std::optional<std::string> pending_login_;
  // Each form made afresh, counted: what remakes it.
  std::uint32_t form_round_ = 0;
};

}  // namespace mux::app
