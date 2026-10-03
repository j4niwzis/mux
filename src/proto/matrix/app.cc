// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto.matrix -- What the program does with Matrix's own changes:
// what its developer tools found, its sticker packs, an emoji verification,
// the account's sessions and its security. Templates on the program, which
// calls program_told(app, change) where it takes changes (mux.app.proto).
export module mux.app.proto.matrix;

import std;
import splice;
import mux.core;
import mux.proto.matrix.changes;
import mux.ui;
import mux.ui.proto.matrix;

export namespace mux::proto::matrix {

// What the developer tools asked, shown.
template <class App>
void program_told(App& app, const devtools_text& shown) {
  app.root().show_devtools_text(shown.title, shown.text);
}
// The room's state, for the developer tools.
template <class App>
void program_told(App& app, const state_listed& listed) {
  app.root().show_room_state(listed.entries);
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
