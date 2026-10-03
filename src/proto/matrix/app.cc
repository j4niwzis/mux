// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto.matrix -- What the program does with Matrix's own changes:
// what its developer tools found, its sticker packs, an emoji verification,
// the account's sessions and its security. Templates on the program, which
// calls program_told(app, change) where it takes changes (mux.app.proto).
export module mux.app.proto.matrix;

import std;
import splice;
import mux.core;
import mux.config;
import mux.proto.matrix.changes;
import mux.proto.matrix.requests;
import mux.ui;
import mux.ui.proto.matrix;

export namespace mux::proto::matrix {

// What the developer tools asked, shown.
template <class App>
void program_told(App& app, const devtools_text& shown) {
  app.root().template open_dialog<devtools_page<typename App::accounts::actions_type>>(shown.title, shown.text);
}
// The room's state, for the developer tools.
template <class App>
void program_told(App& app, const state_listed& listed) {
  app.root().template open_dialog<devtools_page<typename App::accounts::actions_type>>(listed.entries);
}
// Packs: listed, saved, an image uploaded -- in their dialog.
template <class App>
void program_told(App& app, const packs_listed& listed) {
  app.root().show_packs(listed.packs);
}
template <class App>
void program_told(App& app, const pack_saved& saved) {
  app.root().pack_saved(saved.pack, saved.removed, saved.done);
}
template <class App>
void program_told(App& app, const pack_picture_uploaded& uploaded) {
  app.root().pack_picture_uploaded(uploaded.picture, uploaded.done);
}
// An emoji verification, as it goes: its dialog.
template <class App>
void program_told(App& app, const verification_changed& one) {
  app.verifying = std::pair(one.by, one.txn);
  app.root().show_verification(ui::verification_view{one.user, one.device, one.step});
}

// The account's Sessions page, where it is open for that account.
template <class App, class Then>
void on_sessions_page(App& app, const account_id& by, Then then) {
  using accounts = typename App::accounts;
  if (auto* up = app.root().open_panel())
    splice::visit([&](accounts& panel) {
                    if (auto* page = panel.template shown_page<sessions_page<typename accounts::actions_type>>();
                        page && panel.selected == by.address)
                      then(*page);
                  },
                  *up);
}
template <class App>
void program_told(App& app, const sessions_listed& listed) {
  on_sessions_page(app, listed.by, [&](auto& page) { page.show(listed.current, listed.sessions); });
}
template <class App>
void program_told(App& app, const security_state& state) {
  on_sessions_page(app, state.by, [&](auto& page) { page.show_security(state.cross_signing, state.backup); });
}
template <class App>
void program_told(App& app, const sessions_refused& said) {
  on_sessions_page(app, said.by, [&](auto& page) { page.refused(said.why, said.needs_password); });
}

}  // namespace mux::proto::matrix

// What Matrix's UI asks, done: each for the account whose pages are open.
export namespace mux::proto::matrix::request {

// Asked with a passphrase (or the account's password) first: its dialog up,
// for that account.
template <class App, class Purpose>
void with_passphrase(App& app, Purpose purpose) {
  app.with_chosen_account([&](auto&, config::account_t& account) {
    app.keys_of = App::id_of(account);
    app.root().ask_passphrase(purpose);
  });
}
template <class App>
void program_asked(App& app, const setup_cross_signing&) {
  with_passphrase(app, passphrase::cross_signing{});
}
template <class App>
void program_asked(App& app, const restore_cross_signing&) {
  with_passphrase(app, passphrase::recovery{});
}
template <class App>
void program_asked(App& app, const reset_identity&) {
  with_passphrase(app, passphrase::reset_identity{});
}
template <class App>
void program_asked(App& app, const sign_out_unverified&) {
  with_passphrase(app, passphrase::sign_out_unverified{});
}
template <class App>
void program_asked(App& app, const export_room_keys&) {
  with_passphrase(app, passphrase::export_keys{});
}
template <class App>
void program_asked(App& app, const import_room_keys&) {
  with_passphrase(app, passphrase::import_keys{});
}
// Element's Secure Backup: made anew, or deleted.
template <class App>
void program_asked(App& app, const reset_backup&) {
  app.with_chosen_account([&](auto&, config::account_t& account) {
    if (!app.shared.demo())
      app.net->reset_backup(App::id_of(account));
  });
}
template <class App>
void program_asked(App& app, const delete_backup&) {
  app.with_chosen_account([&](auto&, config::account_t& account) {
    if (!app.shared.demo())
      app.net->delete_backup(App::id_of(account));
  });
}
// The sessions: one verified by emoji, some signed out, one renamed, listed.
template <class App>
void program_asked(App& app, const verify_session& one) {
  app.with_chosen_account([&](auto&, config::account_t& account) {
    app.net->verify_start(App::id_of(account), config::address_of(account), one.device);
  });
}
template <class App>
void program_asked(App& app, const sign_out_sessions& one) {
  app.with_chosen_account([&](auto&, config::account_t& account) {
    app.net->sign_out_sessions(App::id_of(account), one.devices, one.password);
  });
}
template <class App>
void program_asked(App& app, const rename_session& one) {
  app.with_chosen_account([&](auto&, config::account_t& account) {
    app.net->rename_session(App::id_of(account), one.device, one.name);
  });
}
// The developer tools: for the room being managed, by its account.
template <class App>
void program_asked(App& app, const explore_state&) {
  const auto chosen = app.managed();
  if (!chosen || app.shared.demo())
    return;
  app.root().close_manage();
  app.net->list_state(*chosen);
}
template <class App>
void program_asked(App& app, const open_send_custom&) {
  using page = devtools_page<typename App::accounts::actions_type>;
  app.root().close_manage();
  app.root().template open_dialog<page>(typename page::send_form_t{});
}
template <class App>
void program_asked(App& app, const send_custom& one) {
  const auto chosen = app.managed();
  if (!chosen || app.shared.demo())
    return;
  app.net->send_custom(*chosen, one.type, one.state_key, one.json);
}
template <class App>
void program_asked(App& app, const verify_them& one) {
  app.net->verify_start(one.by, one.user, std::nullopt);
}
template <class App>
void program_asked(App& app, const refresh_sessions&) {
  app.with_chosen_account([&](auto&, config::account_t& account) { app.net->list_sessions(App::id_of(account)); });
}

}  // namespace mux::proto::matrix::request

// A passphrase or a password given, for what Matrix asked it for: for the
// account whose keys were asked of (keys_of).
export namespace mux::proto::matrix::passphrase {

template <class App, class Given>
void passphrase_given(App& app, const export_keys&, const Given& one) {
  if (auto refused = config::new_passphrase_refused(one.fresh, one.again))
    return app.root().passphrase_refused(*refused);
  if (!app.keys_of)
    return app.root().close_passphrase();
  const char* home = std::getenv("HOME");
  const auto folder = home && *home ? std::filesystem::path(home) / "Downloads" : std::filesystem::current_path();
  // Joined, not formatted: clang 23 crashed on format strings first made in
  // these modules (see app/network.cc).
  const std::string file = "mux-room-keys-" + std::string(config::file_name_of(app.keys_of->address)) + ".txt";
  app.net->export_room_keys(*app.keys_of, (folder / file).string(), one.fresh);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const import_keys&, const Given& one) {
  if (one.file.empty())
    return app.root().passphrase_refused("Type the key file's path.");
  if (!app.keys_of)
    return app.root().close_passphrase();
  app.net->import_room_keys(*app.keys_of, one.file, one.current);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const cross_signing&, const Given& one) {
  if (app.keys_of)
    app.net->setup_cross_signing(*app.keys_of, one.current);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const sign_out_unverified&, const Given& one) {
  if (app.keys_of)
    app.net->sign_out_unverified(*app.keys_of, one.current);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const reset_identity&, const Given& one) {
  if (app.keys_of)
    app.net->setup_cross_signing(*app.keys_of, one.current, true);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const recovery&, const Given& one) {
  if (app.keys_of)
    app.net->restore_cross_signing(*app.keys_of, one.current);
  app.root().close_passphrase();
}

}  // namespace mux::proto::matrix::passphrase
