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
import mux.proto.kept;
import mux.proto.matrix.changes;
import mux.proto.matrix.requests;
import mux.ui;
import mux.ui.proto.matrix;

// What Matrix's glue asks of an account, by the program's network: each
// where the account's client has the call (ask_if_able, in on_account).
namespace mux::proto::matrix::ops {

template <class Net>
void list_sessions(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.list_sessions())) { account.list_sessions(); });
}

template <class Net>
void sign_out_sessions(Net& net, const account_id& by, std::vector<std::string> devices, std::string password) {
  net.on_account(by, [devices = std::move(devices), password = std::move(password)](auto& account) -> decltype(void(account.sign_out_sessions(devices, password))) { account.sign_out_sessions(devices, password); });
}

template <class Net>
void rename_session(Net& net, const account_id& by, std::string device, std::string name) {
  net.on_account(by, [device = std::move(device), name = std::move(name)](auto& account) -> decltype(void(account.rename_session(device, name))) { account.rename_session(device, name); });
}

template <class Net>
void setup_cross_signing(Net& net, const account_id& by, std::string password, bool reset = false) {
  net.on_account(by, [password = std::move(password), reset](auto& account) -> decltype(void(account.setup_cross_signing(password, reset))) { account.setup_cross_signing(password, reset); });
}

template <class Net>
void restore_cross_signing(Net& net, const account_id& by, std::string recovery) {
  net.on_account(by, [recovery = std::move(recovery)](auto& account) -> decltype(void(account.restore_cross_signing(recovery))) { account.restore_cross_signing(recovery); });
}

template <class Net>
void reset_backup(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.reset_backup())) { account.reset_backup(); });
}

template <class Net>
void delete_backup(Net& net, const account_id& by) {
  net.on_account(by, [](auto& account) -> decltype(void(account.delete_backup())) { account.delete_backup(); });
}

template <class Net>
void sign_out_unverified(Net& net, const account_id& by, std::string password) {
  net.on_account(by, [password = std::move(password)](auto& account) -> decltype(void(account.sign_out_unverified(password))) { account.sign_out_unverified(password); });
}

template <class Net>
void export_room_keys(Net& net, const account_id& by, std::string path, std::string passphrase) {
  net.on_account(by, [path = std::move(path), passphrase = std::move(passphrase)](auto& account) -> decltype(void(account.export_room_keys(path, passphrase))) { account.export_room_keys(path, passphrase); });
}

template <class Net>
void import_room_keys(Net& net, const account_id& by, std::string path, std::string passphrase) {
  net.on_account(by, [path = std::move(path), passphrase = std::move(passphrase)](auto& account) -> decltype(void(account.import_room_keys(path, passphrase))) { account.import_room_keys(path, passphrase); });
}

template <class Net>
void list_state(Net& net, const conversation_id& in) {
  net.on_account_of(in, [room = in.id](auto& account) -> decltype(void(account.list_state(room))) { account.list_state(room); });
}

template <class Net>
void send_custom(Net& net, const conversation_id& in, std::string type, std::optional<std::string> key, std::string json) {
  net.on_account_of(in, [room = in.id, type = std::move(type), key = std::move(key), json = std::move(json)](auto& account) -> decltype(void(account.send_custom(room, type, key, json))) {
    account.send_custom(room, type, key, json);
  });
}

}  // namespace mux::proto::matrix::ops

export namespace mux::proto::matrix {

// A session the server gave at login: kept with the account, as its kept
// type keeps one (take_session).
template <class App>
void program_told(App& app, const session_given& given) {
  const auto found = app.find(given.account.address);
  if (found == app.saved.end())
    return;
  splice::visit([&](auto& one) {
                  using kept_defaults::take_session;
                  take_session(one, given);
                },
                found->own);
  (void)app.write();
}
// A registration stage done on the server's own page: opened in the browser,
// and said -- the registration carries on by itself once it is done there.
template <class App>
void program_told(App& app, const registration_page& page) {
  app.ask.open_url(page.url);
  app.root().show_message("Finish registering in your browser",
                          "The server asks for a step it does on its own page, now open in your browser. Once it is done "
                          "there, mux carries on with the registration by itself.");
}
// The server's own sign-in page (OAuth 2.0): opened in the browser, and
// said -- the account carries on by itself once the browser comes back.
template <class App>
void program_told(App& app, const sign_in_page& page) {
  app.ask.open_url(page.url);
  app.root().show_message("Sign in in your browser",
                          "The server's sign-in page is open in your browser. Once you have signed in there, the "
                          "browser comes back to mux, and the account carries on by itself.");
}
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
  app.verification.showing(one.by, one.txn);
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
  app.shared.with_chosen_account([&](auto&, config::account_t& account) {
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
  app.shared.with_chosen_account([&](auto&, config::account_t& account) {
    if (!app.shared.demo())
      ops::reset_backup(*app.net, App::id_of(account));
  });
}
template <class App>
void program_asked(App& app, const delete_backup&) {
  app.shared.with_chosen_account([&](auto&, config::account_t& account) {
    if (!app.shared.demo())
      ops::delete_backup(*app.net, App::id_of(account));
  });
}
// The sessions: one verified by emoji, some signed out, one renamed, listed.
template <class App>
void program_asked(App& app, const verify_session& one) {
  app.shared.with_chosen_account([&](auto&, config::account_t& account) {
    app.net->verify_start(App::id_of(account), config::address_of(account), one.device);
  });
}
template <class App>
void program_asked(App& app, const sign_out_sessions& one) {
  app.shared.with_chosen_account([&](auto&, config::account_t& account) {
    ops::sign_out_sessions(*app.net, App::id_of(account), one.devices, one.password);
  });
}
template <class App>
void program_asked(App& app, const rename_session& one) {
  app.shared.with_chosen_account([&](auto&, config::account_t& account) {
    ops::rename_session(*app.net, App::id_of(account), one.device, one.name);
  });
}
// A room changed as Matrix changes one: the room being managed, by its account.
template <class App>
void program_asked(App& app, const change_room& one) {
  const auto chosen = app.shared.managed();
  if (!chosen || app.shared.demo())
    return;
  app.net->on_account_of(*chosen, [room = chosen->id, change = one.change](auto& account)
                                      -> decltype(void(account.change_room(room, change))) { account.change_room(room, change); });
}
// The developer tools: for the room being managed, by its account.
template <class App>
void program_asked(App& app, const explore_state&) {
  const auto chosen = app.shared.managed();
  if (!chosen || app.shared.demo())
    return;
  app.root().close_manage();
  ops::list_state(*app.net, *chosen);
}
template <class App>
void program_asked(App& app, const open_send_custom&) {
  using page = devtools_page<typename App::accounts::actions_type>;
  app.root().close_manage();
  app.root().template open_dialog<page>(typename page::send_form_t{});
}
template <class App>
void program_asked(App& app, const send_custom& one) {
  const auto chosen = app.shared.managed();
  if (!chosen || app.shared.demo())
    return;
  ops::send_custom(*app.net, *chosen, one.type, one.state_key, one.json);
}
template <class App>
void program_asked(App& app, const verify_them& one) {
  app.net->verify_start(one.by, one.user, std::nullopt);
}
template <class App>
void program_asked(App& app, const refresh_sessions&) {
  app.shared.with_chosen_account([&](auto&, config::account_t& account) { ops::list_sessions(*app.net, App::id_of(account)); });
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
  ops::export_room_keys(*app.net, *app.keys_of, (folder / file).string(), one.fresh);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const import_keys&, const Given& one) {
  if (one.file.empty())
    return app.root().passphrase_refused("Type the key file's path.");
  if (!app.keys_of)
    return app.root().close_passphrase();
  ops::import_room_keys(*app.net, *app.keys_of, one.file, one.current);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const cross_signing&, const Given& one) {
  if (app.keys_of)
    ops::setup_cross_signing(*app.net, *app.keys_of, one.current);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const sign_out_unverified&, const Given& one) {
  if (app.keys_of)
    ops::sign_out_unverified(*app.net, *app.keys_of, one.current);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const reset_identity&, const Given& one) {
  if (app.keys_of)
    ops::setup_cross_signing(*app.net, *app.keys_of, one.current, true);
  app.root().close_passphrase();
}
template <class App, class Given>
void passphrase_given(App& app, const recovery&, const Given& one) {
  if (app.keys_of)
    ops::restore_cross_signing(*app.net, *app.keys_of, one.current);
  app.root().close_passphrase();
}

}  // namespace mux::proto::matrix::passphrase
