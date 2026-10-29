// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.encryption.app -- End-to-end encryption's part of the program:
// an emoji verification begun from a person's card, its dialog shown as it
// goes and its answers given; a person's reset identity accepted.
export module mux.plugin.encryption.app;

import std;
import splice;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.network;
import mux.app.services;
import mux.config;
import mux.proto;
import mux.plugin.encryption.types;
import mux.plugin.encryption.changes;

export namespace mux::plugins::encryption {

class part {
 public:
  explicit part(app::services& shared) : s_(&shared) {}

  // A verification, as its account says it goes: its dialog.
  void take(const verification_changed& one) {
    shown_ = std::pair(one.by, one.txn);
    step_ = one.step;
    ui::show(*s_->showing, std::optional(verification_view{one.user, one.device, one.step}));
  }
  // Begun from a person's card or a session's row.
  void apply(const ui::request::verify_person& one) { s_->net->verify_start(one.who.account, one.who.id, std::nullopt); }
  // The dialog's answers, to the verification it shows.
  void apply(const verify_accept_now&) {
    if (shown_)
      s_->net->verify_accept(shown_->first, shown_->second);
  }
  void apply(const verify_cancel_now&) {
    this->cancel();
    this->close();
  }
  void apply(const verify_match&) { this->confirm(true); }
  void apply(const verify_mismatch&) { this->confirm(false); }
  // Closed -- OK, or Esc -- as its step lets it: over, put away; while it
  // waits, cancelled; not while the emoji are compared.
  void apply(const ui::request::close_shown<verification_view>&) {
    spl::visit(spl::overloaded{[](const verification_step::compare&) {},
                               [&](verification_step::asked) { this->cancel_and_close(); },
                               [&](verification_step::waiting) { this->cancel_and_close(); },
                               [&](const auto&) { this->close(); }},
               step_);
  }
  // From its page: a passphrase, or the account's password, asked first --
  // for the account whose pages are open.
  void apply(const setup_cross_signing&) { this->ask<passphrase::cross_signing>(); }
  void apply(const restore_cross_signing&) { this->ask<passphrase::recovery>(); }
  void apply(const reset_identity&) { this->ask<passphrase::reset_identity>(); }
  void apply(const sign_out_unverified&) { this->ask<passphrase::sign_out_unverified>(); }
  void apply(const export_room_keys&) { this->ask<passphrase::export_keys>(); }
  void apply(const import_room_keys&) { this->ask<passphrase::import_keys>(); }
  // Element's Secure Backup: made anew, or deleted.
  void apply(const reset_backup&) {
    this->on_chosen([](auto& account) -> decltype(void(account.reset_backup())) { account.reset_backup(); });
  }
  void apply(const delete_backup&) {
    this->on_chosen([](auto& account) -> decltype(void(account.delete_backup())) { account.delete_backup(); });
  }
  // A person's reset identity accepted ("Withdraw verification").
  void apply(const ui::request::accept_identity& one) {
    if (!s_->demo())
      s_->net->accept_identity(one.who.account, one.who.id);
  }

 private:
  template <class Purpose>
  void ask() {
    s_->with_chosen_account([&](config::account_t& account) {
      ui::show(*s_->showing, std::optional(ui::passphrase_facts{Purpose{chosen_id(account)}, std::nullopt}));
    });
  }
  template <class Work>
  void on_chosen(Work work) {
    s_->with_chosen_account([&](config::account_t& account) {
      if (!s_->demo())
        s_->net->on_account(chosen_id(account), work);
    });
  }
  [[nodiscard]] static account_id chosen_id(const config::account_t& account) {
    const std::string address(config::address_of(account));
    return account_id{protocol_of(address), address};
  }
  void confirm(bool match) {
    if (shown_)
      s_->net->verify_confirm(shown_->first, shown_->second, match);
  }
  void cancel() {
    if (shown_)
      s_->net->verify_cancel(shown_->first, shown_->second);
  }
  void cancel_and_close() {
    this->cancel();
    this->close();
  }
  void close() {
    shown_.reset();
    ui::show<verification_view>(*s_->showing, std::nullopt);
  }

  app::services* s_;
  std::optional<std::pair<account_id, std::string>> shown_;
  // Its step as last told: none shown, one over.
  verification_step_t step_{verification_step::done{}};
};

// A passphrase or the account's password given, for what it was asked for
// (contract::passphrases): done for its account, and the dialog put away
// -- or, where it will not do, said why.
namespace passphrase {
template <class App, class Given>
void passphrase_given(App& app, const export_keys& why, const Given& one) {
  if (auto refused = config::new_passphrase_refused(one.fresh, one.again))
    return app.shared.passphrase_refused(*refused);
  const char* home = std::getenv("HOME");
  const auto folder = home && *home ? std::filesystem::path(home) / "Downloads" : std::filesystem::current_path();
  // Joined, not formatted: clang 23 crashed on format strings first made in
  // these modules (see app/network.cc).
  const std::string file = "mux-room-keys-" + std::string(config::file_name_of(why.by.address)) + ".txt";
  app.shared.net->on_account(why.by, [path = (folder / file).string(), secret = one.fresh](auto& account)
                                          -> decltype(void(account.export_room_keys(path, secret))) { account.export_room_keys(path, secret); });
  ui::show<ui::passphrase_facts>(*app.shared.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const import_keys& why, const Given& one) {
  if (one.file.empty())
    return app.shared.passphrase_refused("Type the key file's path.");
  app.shared.net->on_account(why.by, [path = one.file, secret = one.current](auto& account)
                                          -> decltype(void(account.import_room_keys(path, secret))) { account.import_room_keys(path, secret); });
  ui::show<ui::passphrase_facts>(*app.shared.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const cross_signing& why, const Given& one) {
  app.shared.net->on_account(why.by, [password = one.current](auto& account)
                                          -> decltype(void(account.setup_cross_signing(password, false))) { account.setup_cross_signing(password, false); });
  ui::show<ui::passphrase_facts>(*app.shared.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const reset_identity& why, const Given& one) {
  app.shared.net->on_account(why.by, [password = one.current](auto& account)
                                          -> decltype(void(account.setup_cross_signing(password, true))) { account.setup_cross_signing(password, true); });
  ui::show<ui::passphrase_facts>(*app.shared.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const sign_out_unverified& why, const Given& one) {
  app.shared.net->on_account(why.by, [password = one.current](auto& account)
                                          -> decltype(void(account.sign_out_unverified(password))) { account.sign_out_unverified(password); });
  ui::show<ui::passphrase_facts>(*app.shared.showing, std::nullopt);
}
template <class App, class Given>
void passphrase_given(App& app, const recovery& why, const Given& one) {
  app.shared.net->on_account(why.by, [recovery_key = one.current](auto& account)
                                          -> decltype(void(account.restore_cross_signing(recovery_key))) { account.restore_cross_signing(recovery_key); });
  ui::show<ui::passphrase_facts>(*app.shared.showing, std::nullopt);
}
}  // namespace passphrase

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::encryption
