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
  // A person's reset identity accepted ("Withdraw verification").
  void apply(const ui::request::accept_identity& one) {
    if (!s_->demo())
      s_->net->accept_identity(one.who.account, one.who.id);
  }

 private:
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

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{shared}; }

}  // namespace mux::plugins::encryption
