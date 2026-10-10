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
import skiff.model;
import skiff.bind;
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
  // What the window shows that the program opens and closes.
  mux::ui::shown_model* showing = nullptr;
  // And the binding the window reads it through: read at once only where
  // nothing of the window is being answered -- what the network told.
  skiff::bind::Binding<mux::ui::shown_model>* showing_binding = nullptr;
  // A message's menu asked for: focused once it is made.
  bool menu_focus_due = false;
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
  // The whole-window binding is instantiated in one implementation unit.
  void read_shown_now() const;
  // A notice shown, or the one up closed: what the window shows, edited.
  void notice(std::string heading, std::string text) const {
    mux::ui::show(*showing, std::optional(mux::ui::notice_facts{std::move(heading), std::move(text)}));
  }
  void not_implemented(std::string what) const {
    this->notice("Not implemented yet", std::format("{} isn't implemented yet.", what));
  }
  // The emoji panel open where it is put, or closed -- the field back where
  // it was; and whether it is open, as what is shown says.
  void open_emoji(float right, float bottom) const {
    const auto chosen = root().main().chosen;
    const auto ops = chosen ? mux::ui::ops_of(ui, chosen->account) : mux::proto::account_ops{};
    mux::ui::show(*showing, std::optional(mux::ui::emoji_facts{right, bottom, {}, mux::ui::popup_page::emoji{},
        ops.send_sticker, chosen && mux::ui::may_send_files(ui, chosen->account)}));
  }
  void close_emoji() const {
    mux::ui::show<mux::ui::emoji_facts>(*showing, std::nullopt);
    this->root().emoji_closed();
  }
  [[nodiscard]] bool emoji_open() const { return showing->root().emoji.fValue.has_value(); }
  // The threads' panel, as what is shown says: opened on a thread, the
  // thread closed in it, or the panel toggled -- whether it is open now.
  void open_thread(std::string root) const {
    mux::ui::change_shown<mux::ui::chat_shown>(*showing, [&](mux::ui::chat_shown& now) {
      now.threads_open = true;
      now.thread = std::move(root);
    });
  }
  void close_thread() const {
    mux::ui::change_shown<mux::ui::chat_shown>(*showing, [](mux::ui::chat_shown& now) { now.thread.reset(); });
  }
  bool toggle_threads() const {
    mux::ui::change_shown<mux::ui::chat_shown>(*showing, [](mux::ui::chat_shown& now) {
      now.threads_open = !now.threads_open;
      now.thread.reset();
    });
    return showing->look<mux::ui::chat_shown>()->threads_open;
  }
  // A page of settings shown, where settings are open.
  void settings_page(mux::ui::settings_page_t page) const {
    if (showing->look<std::optional<mux::ui::settings_facts>>()->has_value())
      mux::ui::show(*showing, std::optional(mux::ui::settings_facts{std::move(page)}));
  }
  // The passphrase given refused: why, said in its box.
  void passphrase_refused(std::string why) const {
    mux::ui::change_shown<std::optional<mux::ui::passphrase_facts>>(*showing, [&](auto& now) {
      if (now) {
        now->refused = std::move(why);
        now->current.clear();
        now->fresh.clear();
        now->again.clear();
      }
    });
  }
  void close_notice() const { mux::ui::show<mux::ui::notice_facts>(*showing, std::nullopt); }
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
          // A copy changed, and put back in the model where it changed.
          kept->change_account(*panel.selected, [&](mux::config::account_t& account) { f(panel, account); });
        },
        *up);
  }
  // The chat Manage is for: a space, where its settings are open -- what is
  // chosen there goes to it -- else the chat chosen.
  std::optional<mux::conversation_id> manage_target;
  [[nodiscard]] std::optional<mux::conversation_id> managed() const {
    return manage_target && root().manage_up() ? manage_target : root().main().chosen;
  }
  // Features belong to the selected account. An unsupported feature never
  // borrows another account's protocol or credentials.
  template <class Feature>
  [[nodiscard]] std::optional<mux::account_id> account_offering(Feature wanted) const {
    const auto current = root().main().current;
    return current && mux::proto::offers(mux::ui::protocol_state_of(ui, *current), wanted)
        ? current : std::nullopt;
  }
};

}  // namespace mux::app
