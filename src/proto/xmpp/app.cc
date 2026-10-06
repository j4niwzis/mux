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

namespace mux::proto::xmpp::app_detail {
// What the server asks, in the form that shows it -- XMPP's -- and in no other.
template <class Form>
  requires requires(Form& form, const registration_asked& asked) { form.show_registration(asked); }
void show_in(Form& form, const registration_asked& asked) {
  form.show_registration(asked);
}
void show_in(auto&, const registration_asked&) {}
}  // namespace mux::proto::xmpp::app_detail

export namespace mux::proto::xmpp {

// What the server asks beyond the address and the password: its fields --
// a captcha among them, of whatever kind -- in the account's settings, to
// be answered and saved, which connects again with the answers. A server
// that makes accounts only on a page of its own: that page, in the browser.
template <class App>
void program_told(App& app, const registration_asked& asked) {
  if (asked.fields.empty() && asked.page) {
    app.ask.open_url(*asked.page);
    app.root().show_message("Register in your browser",
                            "The server makes accounts on a page of its own, now open in your browser. Once the account "
                            "is made there, turn Create a new account off in its settings, and mux signs in to it.");
    return;
  }
  auto& panel = app.accounts_screen.show_account(asked.account.address);
  if (auto* editor = panel.editor())
    spl::visit([&](auto& form) { app_detail::show_in(form, asked); }, editor->parts.form);
}
// The account made: kept as one to sign in to from now on.
template <class App>
void program_told(App& app, const registered& made) {
  const auto found = app.find(made.account.address);
  if (found == app.saved.end())
    return;
  spl::visit(spl::overloaded{[](kept& one) { forget_registration(one); }, [](auto&) {}}, found->own);
  (void)app.write();
}

}  // namespace mux::proto::xmpp
