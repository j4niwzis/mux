// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.verification: a person's or a session's identity -- an emoji
// verification begun, answered in its dialog and closed; a reset identity
// accepted. A part of the program: it owns its state, and reaches the rest
// through the services it is given.
export module mux.app.verification;

import std;
import mux.core;
import mux.ui;
import mux.ui.proto;
import mux.app.network;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class verification_part {
 public:
  explicit verification_part(services& shared) : s_(&shared) {}
  verification_part(const verification_part&) = delete;
  verification_part& operator=(const verification_part&) = delete;

  // The verification its dialog shows now, as its protocol says it goes:
  // its account, and its transaction -- what the dialog's answers are for.
  void showing(mux::account_id by, std::string txn) { shown_ = std::pair(std::move(by), std::move(txn)); }

  // Begun from a person's card or a session's row.
  void apply(const request::verify_person& one) { s_->net->verify_start(one.who.account, one.who.id, std::nullopt); }
  // The dialog's answers, to the verification it shows.
  void apply(const request::verify_accept_now&) {
    if (shown_)
      s_->net->verify_accept(shown_->first, shown_->second);
  }
  void apply(const request::verify_cancel_now&) {
    if (shown_)
      s_->net->verify_cancel(shown_->first, shown_->second);
    s_->root().close_verification();
  }
  void apply(const request::verify_match&) { this->confirm(true); }
  void apply(const request::verify_mismatch&) { this->confirm(false); }
  void apply(const request::close_verification&) {
    shown_.reset();
    s_->root().close_verification();
  }
  // A person's reset identity accepted ("Withdraw verification").
  void apply(const request::accept_identity& one) {
    if (!s_->demo())
      s_->net->accept_identity(one.who.account, one.who.id);
  }

 private:
  void confirm(bool match) {
    if (shown_)
      s_->net->verify_confirm(shown_->first, shown_->second, match);
  }

  services* s_;
  std::optional<std::pair<mux::account_id, std::string>> shown_;
};

}  // namespace mux::app
