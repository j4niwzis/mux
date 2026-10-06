// SPDX-License-Identifier: AGPL-3.0-only
// The program: accounts, their pages, privacy and proxies.
module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.logic.links;
import mux.protocols;
import mux.logic.room_events;
import mux.config;
import mux.net;
import mux.media;
import mux.platform.dialogs;
import mux.platform.push;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

// A person's info: a box in the middle of the window, as tdesktop's.
void app::apply(const request::open_member_info& one) {
  const auto chosen = shared.managed();
  if (!chosen)
    return;
  const mux::conversation* in = model->find(*chosen);
  root().open_person(chosen->account, one.id, mux::ui::person_of(shared.ui, in, *model, chosen->account, one.id));
  person_open_ = std::pair{*chosen, one.id};
  if (!shared.demo()) {
    net->ask_trust(chosen->account, one.id);
    net->ask_devices(chosen->account, one.id);
  }
}

void app::apply(const request::close_person_info&) {
  person_open_.reset();
  root().close_person();
}

void app::apply(const request::copy_text& one) {
  skiff::scene::setClipboardText(one.text);
  root().close_text_menu();
}
// A field's text menu: its item's key given to the field with the focus,
// with Ctrl -- the field's own paste, cut, copy or select all -- and the
// menu gone.
void app::apply(const request::text_key& one) {
  skiff::scene::giveKey(
      {one.key, skiff::scene::Modifiers{}.with<skiff::scene::modifier::control>(true).with<skiff::scene::modifier::shift>(one.shift)});
  root().close_text_menu();
}

// The developer tools, for the chat being read.
void app::apply(const request::close_dialog&) { root().close_dialog(); }

void app::apply(const request::not_implemented& one) { root().show_notice(one.what); }

void app::apply(const request::close_notice&) { root().close_notice(); }

void app::apply(const request::resize_info& one) { root().main().resize_info(one.x); }

void app::apply(const request::accounts_back&) {
  auto* up = root().open_panel();
  if (!up)
    return;
  spl::visit(
      [this](accounts& panel) {
        if (panel.step_back())
          return;
        if (panel.pages_open())
          panel.close_pages();
        else
          this->apply(request::pop_panel{});
      },
      *up);
}

// A tombstoned room's way on: the room it was upgraded to, as its
// protocol's link to it opens -- the chat where it is joined, its card where not.
void app::apply(const request::open_replacement&) {
  const auto& chosen = root().main().chosen;
  const mux::conversation* chat = chosen ? model->find(*chosen) : nullptr;
  if (const auto successor = chat ? mux::proto::successor_of(mux::ui::protocol_state_of(shared.ui, chosen->account), *chat) : std::nullopt)
    if (auto link = mux::proto::share_link(mux::ui::protocol_state_of(shared.ui, chosen->account), *successor))
      this->apply(request::open_url{std::move(*link)});
}

}  // namespace mux::app
