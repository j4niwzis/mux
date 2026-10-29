// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto.xmpp -- What the program does with XMPP's own changes: what
// the server asks to make an account (XEP-0077), shown in the account's
// form, and the account made. Templates on the program, which calls
// program_told(app, change) where it takes changes (mux.app.proto).
export module mux.app.proto.xmpp;

import std;
import splice;
import mux.core;
import mux.config;
import mux.proto.kept;
import mux.proto.xmpp.changes;
import mux.ui;
import mux.proto.xmpp;
import mux.app.network;


export namespace mux::proto::xmpp {

// What the server asks beyond the address and the password: its fields --
// a captcha among them, of whatever kind -- in the account's settings, to
// be answered and saved, which connects again with the answers. A server
// that makes accounts only on a page of its own: that page, in the browser.
template <class App>
void program_told(App& app, const registration_asked& asked) {
  if (asked.fields.empty() && asked.page) {
    app.take(mux::ui::request::open_url{*asked.page});
    app.shared.notice("Register in your browser",
                            "The server makes accounts on a page of its own, now open in your browser. Once the account "
                            "is made there, turn Create a new account off in its settings, and mux signs in to it.");
    return;
  }
  app.accounts_screen.form_part_told(asked.account.address, registration_form{asked, first_answers(asked)},
                                     "The server asks more to make the account: answer below, then save.");
}
// The account made: kept as one to sign in to from now on.
template <class App>
void program_told(App& app, const registered& made) {
  app.change_account(made.account.address, [](mux::config::account_t& account) {
    spl::visit(spl::overloaded{[](kept& one) { forget_registration(one); }, [](auto&) {}}, account.own);
  });
}

}  // namespace mux::proto::xmpp

export namespace mux::proto::xmpp::request {

// One of the fields it asked, typed into: the answer kept beside the form.
template <class App>
void program_asked(App& app, const registration_typed& typed) {
  if (const auto& open = *app.showing.template look<std::optional<mux::ui::account_form_facts>>()) {
    auto now = *open;
    spl::visit(spl::overloaded{[&](registration_form& kept) {
                                 const auto found = std::ranges::find(kept.answers, typed.var, &registration_answer::var);
                                 if (found != kept.answers.end())
                                   found->value = typed.value;
                               },
                               [](auto&) {}},
               now.part);
    mux::ui::show(app.showing, std::optional(std::move(now)));
  }
}

}  // namespace mux::proto::xmpp::request
