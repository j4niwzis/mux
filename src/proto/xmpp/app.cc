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

namespace mux::proto::xmpp::app_detail {
// The room the settings show, its configuration changed as `change` says,
// and the page shown again from it.
template <class App, class Change>
void change_room(App& app, const mux::conversation_id& in, Change change) {
  if (auto* panel = app.root().manage_up(); panel && panel->facts.chat == in)
    spl::visit(spl::overloaded{[&](muc_room& room) {
                                 change(room);
                                 panel->show_again();
                               },
                               [](auto&) {}},
               panel->facts.theirs);
}
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
    app.take(mux::ui::request::open_url{*asked.page});
    app.shared.notice("Register in your browser",
                            "The server makes accounts on a page of its own, now open in your browser. Once the account "
                            "is made there, turn Create a new account off in its settings, and mux signs in to it.");
    return;
  }
  auto* panel = app.accounts_screen.show_account_now(asked.account.address);
  if (auto* editor = panel ? panel->editor() : nullptr)
    spl::visit([&](auto& form) { app_detail::show_in(form, asked); }, editor->parts.form);
}
// The account made: kept as one to sign in to from now on.
template <class App>
void program_told(App& app, const registered& made) {
  app.change_account(made.account.address, [](mux::config::account_t& account) {
    spl::visit(spl::overloaded{[](kept& one) { forget_registration(one); }, [](auto&) {}}, account.own);
  });
}

template <class App>
void program_told(App& app, const muc_settings_loaded& loaded) {
  const auto chosen = app.shared.managed();
  if (!chosen || chosen->account != loaded.account || chosen->id != loaded.room) return;
  if (auto* panel = app.root().manage_up()) {
    panel->facts.theirs = loaded.settings;
    panel->show_again();
  }
}
template <class App>
void program_told(App& app, const muc_configuration_failed& failed) {
  const auto chosen = app.shared.managed();
  if (!chosen || chosen->account != failed.account || chosen->id != failed.room) return;
  // The save done with: what the server said, beside the fields as they
  // were sent.
  app_detail::change_room(app, *chosen, [&](muc_room& room) {
    room.saving = false;
    room.failure = failed.reason;
  });
}
template <class App, class Request>
void muc_request(App& app, const Request& one) {
  const auto chosen = app.shared.managed();
  if (!chosen || app.shared.demo()) return;
  app.net->on_account_of(*chosen, [room = chosen->id, one](auto& account)
      -> decltype(void(account.change_muc(room, one))) { account.change_muc(room, one); });
}
}  // namespace mux::proto::xmpp

export namespace mux::proto::xmpp::request {

template <class App> void program_asked(App& app, const refresh_muc& one) { muc_request(app, one); }
// A configuration saved: on its way, the fields as sent kept with the room
// until the server answers -- loaded again, or what was wrong.
template <class App> void program_asked(App& app, const configure_muc& one) {
  muc_request(app, one);
  if (const auto chosen = app.shared.managed())
    app_detail::change_room(app, *chosen, [&](muc_room& room) {
      room.saving = true;
      room.failure.reset();
      room.fields = one.fields;
    });
}
template <class App> void program_asked(App& app, const muc_identity& one) { muc_request(app, one); }
template <class App> void program_asked(App& app, const muc_privilege& one) { muc_request(app, one); }
template <class App> void program_asked(App& app, const destroy_muc& one) { muc_request(app, one); }
template <class App> void program_asked(App& app, const rejoin_muc& one) { muc_request(app, one); }

}  // namespace mux::proto::xmpp::request
