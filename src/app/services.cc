// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.services: what every part of the program works with -- the model,
// the network, the messages on disk, the window, and asking for the window
// to be brought up to date -- handed to each part, and nothing else of the
// others: a part owns its own state, and reaches the rest through this.
export module mux.app.services;

import std;
import splice;
import mux.platform.dialogs;
import mux.platform.audio;
import mux.vault;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.protocols;
import mux.logic.links;
import mux.app.network;
import mux.app.workers;
import mux.app.store;
import mux.app.requests;
import mux.app.kept;

export namespace mux::app {

struct services {
  mux::model* model = nullptr;
  // The emoji and stickers kept, as the window's panels show them.
  mux::ui::emoji_kept emoji;
  // The looks the window shows.
  mux::ui::looks_shown looks;
  // How fills are painted: handed to the host, which draws with it.
  mux::ui::mux_paint paint;
  // What the window's parts tell one another and the program.
  mux::ui::ui_shared ui;
  network* net = nullptr;
  message_store* store = nullptr;
  mailbox_type* box = nullptr;
  actions* ask = nullptr;
  skiff::scene::Scene<window_type>* scene = nullptr;
  // What a part leaves the program to do once it is done, as data: the
  // window brought up to date with the model a part changed; the window
  // made again, in a theme or an accent chosen.
  bool refresh_due = false;
  bool rebuild_due = false;
  // Whether it is used by a finger -- a phone's screen -- as the host saw
  // last: a touch, or a mouse's press. Where it is, the input is not given
  // the keys' focus on its own: that started the text input, and with it a
  // phone's on-screen keyboard, over half the screen whenever a chat was
  // open. The keyboard comes up as the field is tapped, as on Telegram's.
  bool by_touch = false;
  // The proxy chosen for the account being added, as it is added.
  // What the message field's text is: a new message, an answer to one, or
  // one edited; and the message whose menu is up.
  // The drawer, left open under a page coming in over it, to go when the
  // page is in.
  bool drawer_waits = false;
  // A chat to open, a link to follow -- its card, or its room -- once a part
  // is done; and a room joined, from a link or the directory, opened once
  // the model has it.
  std::optional<mux::conversation_id> chat_due;
  std::optional<mux::logic::link_t> link_due;
  std::optional<mux::logic::link_t> joining;
  // The settings as saved -- an account's privacy, its proxy, by its
  // address -- the program's.
  kept_settings* kept = nullptr;
  mux::vault::vault* vault = nullptr;
  // Work off the UI's thread.
  workers* work = nullptr;
  mux::platform::dialogs::dialogs* system_dialogs = nullptr;
  // What wakes the window from another thread.
  wake_window* wake = nullptr;
  // What plays voice messages.
  mux::platform::audio::speaker* speaker = nullptr;

  [[nodiscard]] window_type& root() const { return scene->root(); }
  // A chat that is a window of its history away from its newest: back to
  // its newest, live -- before anything is put at its end. Its newest from
  // the disk, where all that came meanwhile is kept.
  static constexpr std::size_t kLiveFromDisk = 400;
  void go_live(const conversation_id& in) {
    const mux::conversation* chat = model->find(in);
    if (!chat || !chat->detached)
      return;
    model->apply(mux::change_t{mux::change::window_opened{in, std::string(), std::nullopt}});
    // As much as a chat holds in memory, not a screenful: going back from a
    // jump to where it came from, what was shown around it was made a window
    // of the server's of its own and lost.
    for (auto& one : store->older(in, message_store::time_point::max(), kLiveFromDisk))
      model->apply(
          mux::change_t{mux::change::message_added{.message = std::move(one), .where = mux::placement::in_window{}}});
    refresh_due = true;
  }
  // The demo: no network, and nothing kept.
  [[nodiscard]] bool demo() const { return ask->demo; }

  // The chosen account, and its accounts page, when they are up.
  template <class F>
  void with_chosen_account(F&& f) {
    auto* up = root().open_panel();
    if (!up)
      return;
    spl::visit(
        [&](mux::ui::accounts_panel<actions>& panel) {
          if (!panel.selected)
            return;
          if (const auto found = kept->find(*panel.selected); found != kept->saved.end())
            f(panel, *found);
        },
        *up);
  }
  // The chat Manage is for: a space, where its settings are open -- what is
  // chosen there goes to it -- else the chat chosen.
  std::optional<mux::conversation_id> manage_target;
  [[nodiscard]] std::optional<mux::conversation_id> managed() const {
    return manage_target && root().manage_up() ? manage_target : root().main().chosen;
  }
  // An account whose protocol offers what is asked: the one in view where
  // it does, else the first.
  template <class Feature>
  [[nodiscard]] std::optional<mux::account_id> account_offering(Feature wanted) const {
    if (const auto& current = root().main().current; current && mux::proto::offers(mux::ui::protocol_state_of(ui, *current), wanted))
      return current;
    for (const auto& [id, account] : model->accounts())
      if (mux::proto::offers(mux::ui::protocol_state_of(ui, id), wanted))
        return id;
    return std::nullopt;
  }
};

}  // namespace mux::app
