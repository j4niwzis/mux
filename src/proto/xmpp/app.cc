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
  const auto& open = *app.showing.template look<std::optional<mux::ui::room_settings_facts>>();
  if (!open || open->chat != in)
    return;
  auto now = *open;
  spl::visit(spl::overloaded{[&](muc_room& room) { change(room); }, [](auto&) {}}, now.theirs);
  mux::ui::show(app.showing, std::optional(std::move(now)));
}
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

template <class App>
void program_told(App& app, const muc_settings_loaded& loaded) {
  const auto chosen = app.shared.managed();
  if (!chosen || chosen->account != loaded.account || chosen->id != loaded.room) return;
  if (const auto& open = *app.showing.template look<std::optional<mux::ui::room_settings_facts>>()) {
    auto now = *open;
    now.theirs = loaded.settings;
    mux::ui::show(app.showing, std::optional(std::move(now)));
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
