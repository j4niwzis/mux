// SPDX-License-Identifier: AGPL-3.0-only
// The program: messages sent and the motion setting.
module mux.app.program;

import std;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

void app::apply(const request::switch_account& one) {
  auto& screen = root();
  // What was being written in the chat left: kept as its draft, and the
  // field emptied. Left in the field, the next chat opened put its own
  // draft over it -- paragraphs written were gone (a user's).
  if (const auto& chosen = shared.chosen()) {
    drafts.keep(*chosen, mux::ui::draft_text(shared.chat_writing()));
    shared.write_chat({});
  }
  mux::ui::change_shown<mux::ui::chat_shown>(showing, [&](mux::ui::chat_shown& now) {
    now.current = mux::account_id{mux::ui::protocol_of(one.address), one.address};
    now.chosen.reset();
  });
  this->change_part<recently_used>([&](recently_used& now) { now.last_account = one.address; });
  mux::ui::show(showing, mux::ui::drawer_shown{false});
  this->refresh_shown();
  this->refresh();
}

void app::apply(const request::pop_panel&) {
  accounts_screen.forget_login();
  mux::ui::show<mux::ui::panel_facts>(showing, std::nullopt);
  mux::ui::show<mux::ui::account_page_facts>(showing, std::nullopt);
  mux::ui::show<mux::ui::account_form_facts>(showing, std::nullopt);
  this->refresh();
}

}  // namespace mux::app
