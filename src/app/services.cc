// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.services: what every part of the program works with -- the model,
// the network, the messages on disk, the window, and asking for the window
// to be brought up to date -- handed to each part, and nothing else of the
// others: a part owns its own state, and reaches the rest through this.
export module mux.app.services;

import std;
import splice;
import skiff.platform.dialogs;
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

// A notification mux shows itself, in a window of its own: for the host --
// a plugin's (the notifications'), gathered by the program.
struct toast_due {
  mux::conversation_id chat;
  std::string key;
  std::string title;
  std::string text;
};

struct services {
  mux::model* model = nullptr;
  // What the window shows that the program opens and closes.
  mux::ui::shown_model* showing = nullptr;
  // And the binding the window reads it through: read at once only where
  // nothing of the window is being answered -- what the network told.
  skiff::bind::Binding<mux::ui::shown_model>* showing_binding = nullptr;
  // A message's menu asked for: focused once it is made.
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
  // How much is kept, in memory and on disk, in force: what a plugin (the
  // storage plugin's settings) says, the defaults where none does; and it
  // changed, to be told to the caches and the disk after the event.
  mux::config::cache_limits limits;
  bool limits_due = true;
  // What is done to a picture dropped before it is sent, in force: what a
  // plugin (the files plugin's settings) says; nothing where none does.
  mux::config::sending_settings sending;
  mailbox_type* box = nullptr;
  actions* ask = nullptr;
  skiff::scene::Scene<window_type>* scene = nullptr;
  // What a part leaves the program to do once it is done, as data: the
  // window brought up to date with the model a part changed; the window
  // made again, in a theme or an accent chosen.
  bool refresh_due = false;
  // A list over the composer is up (a plugin's: what an @ or a ':' asks):
  // the screen catches its keys -- Up, Down, Enter, Tab, Esc -- for it.
  bool composer_list_up = false;
  // Finding in the chat is open (a plugin's): Esc closes it, the head
  // stands aside for its bar.
  bool searching = false;
  // The room a card is up for (a plugin's): the server's answer of it is
  // the card's, not a pill's.
  std::optional<std::string> card_room;
  // Whether the window has the keyboard's focus, and is on screen at all:
  // a message to the chat being read then notifies nothing.
  bool window_focused = true;
  bool on_screen = true;
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
  // A chat to open, a link to follow -- its card, or its room -- once a part
  // is done; and a room joined, from a link or the directory, opened once
  // the model has it.
  std::optional<mux::conversation_id> chat_due;
  std::optional<mux::logic::link_t> link_due;
  // A message of the chat open to go to, as a jump to it goes -- its
  // context loaded where it is not: asked by a part from what it was told
  // (a command's jump to a date), done after the event.
  std::optional<mux::ui::request::jump_to_message> jump_due;
  // Whole pictures a part wants fetched -- a plugin's (the viewer's) -- as
  // the pictures part fetches a message's: taken by it at the next frame.
  std::vector<std::pair<mux::account_id, std::string>> whole_wanted;
  // The emoji panel to be put away -- a message sent from under it: by its
  // part (a plugin's), at the next frame.
  bool emoji_close_due = false;
  // The chat a room's settings are open for (the room_settings plugin's).
  std::optional<mux::conversation_id> managing;
  // The chat's list as the jumps part has it -- where a jump goes, and the
  // messages the reader sees, by the rows at the view's top and bottom --
  // read by the other parts; and what they ask of it: a message brought
  // into view (a search's find, an edit or an answer stepped to), or the
  // newest (what was sent): done by the program at the next frame.
  struct list_now {
    std::optional<std::string> jump_target, top, bottom;
  } list;
  // The stretches whose pictures were last asked for -- the chat's and the
  // thread's, by the ids at their ends: asked again only where what is in
  // view comes near their ends, not at every row scrolled past.
  struct asked_span {
    std::optional<std::string> first, last;
  };
  asked_span chat_pictures_span, thread_pictures_span;
  // Whether what is in view (its top and bottom rows) comes within `margin`
  // of the span's ends, or out of it, in the messages given: their pictures
  // to be asked for again.
  [[nodiscard]] static const std::string& id_in(const mux::message& one) { return one.id; }
  [[nodiscard]] static const std::string& id_in(const mux::message* one) { return one->id; }
  [[nodiscard]] static bool nears_end(const auto& messages, const asked_span& span, const std::optional<std::string>& top,
                                      const std::optional<std::string>& bottom, std::ptrdiff_t margin = 10) {
    const auto place = [&](const std::optional<std::string>& id) -> std::optional<std::ptrdiff_t> {
      if (!id)
        return std::nullopt;
      const auto at = std::ranges::find_if(messages, [&](const auto& one) { return id_in(one) == *id; });
      return at == std::ranges::end(messages) ? std::nullopt : std::optional(std::ranges::distance(std::ranges::begin(messages), at));
    };
    const std::ptrdiff_t count = std::ranges::distance(messages);
    // An end not among them -- a thread's root, before its answers -- is
    // their own end.
    const auto first = span.first ? place(span.first).value_or(0) : std::optional<std::ptrdiff_t>();
    const auto last = span.last ? place(span.last).value_or(count - 1) : std::optional<std::ptrdiff_t>();
    const auto seen_top = place(top), seen_bottom = place(bottom);
    if (!first || !last || !seen_top || !seen_bottom)
      return true;
    return (*first > 0 && *seen_top < *first + margin) || (*last < count - 1 && *seen_bottom > *last - margin);
  }
  // The thread's list in its panel: the rows at its view's top and bottom.
  struct thread_list_now {
    std::optional<std::string> top, bottom;
    friend bool operator==(const thread_list_now&, const thread_list_now&) = default;
  } thread_list;
  struct message_asked {
    std::string id;
    std::optional<std::string> fragment;
  };
  std::optional<message_asked> show_message_due;
  bool newest_due = false;
  // A text sent, or asked as a command: the ways back from jumps let go.
  bool returns_due = false;
  // Where the pointer was last pressed: where a menu that press asked for
  // goes.
  skia::SkPoint last_press{};
  // A right press to be given again, once what took it is gone -- a menu
  // closed by it: the window does, after its next layout.
  std::optional<skia::SkPoint> press_again;
  // What is left written in each chat, as the program keeps it; and how
  // many times that changed, for what lists the chats to see it moved.
  std::map<conversation_id, std::string> drafts;
  std::uint64_t drafts_revision = 0;
  [[nodiscard]] std::string draft_of(const conversation_id& id) const {
    const auto found = drafts.find(id);
    return found == drafts.end() ? std::string() : found->second;
  }
  // What is written in the chat's field and the thread's, as the program
  // keeps it -- the fields show it; and changed, as the program changes it
  // (a link put on, a name picked, a quote) or a field told it was edited.
  [[nodiscard]] const mux::ui::composer_doc& chat_writing() { return showing->look<mux::ui::chat_draft>()->now; }
  [[nodiscard]] const mux::ui::composer_doc& thread_writing() { return showing->look<mux::ui::thread_draft>()->now; }
  void write_chat(mux::ui::composer_doc now) { mux::ui::show(*showing, mux::ui::chat_draft{std::move(now)}); }
  void write_thread(mux::ui::composer_doc now) { mux::ui::show(*showing, mux::ui::thread_draft{std::move(now)}); }
  template <class Change>
  void edit_chat_writing(Change change) {
    auto now = this->chat_writing();
    change(now);
    this->write_chat(std::move(now));
  }
  template <class Change>
  void edit_thread_writing(Change change) {
    auto now = this->thread_writing();
    change(now);
    this->write_thread(std::move(now));
  }
  // An answer being written in the thread open let go: the field's bar over
  // it too.
  void stop_thread_answer() const {
    const auto& shown = *showing->look<mux::ui::chat_shown>();
    if (!shown.thread_answer && !shown.thread_edit)
      return;
    mux::ui::change_shown<mux::ui::chat_shown>(*showing, [](mux::ui::chat_shown& now) {
      now.thread_answer.reset();
      now.thread_edit.reset();
    });
    mux::ui::show(*showing, mux::ui::thread_compose{});
  }
  // A message of the thread open edited there: "Edit message" over its
  // field. The chat's field, behind the thread on a phone, took it before,
  // and Edit seemed to do nothing (a tester's).
  void edit_in_thread(std::string id, mux::ui::compose_context said) const {
    mux::ui::change_shown<mux::ui::chat_shown>(*showing, [&](mux::ui::chat_shown& now) {
      now.thread_answer.reset();
      now.thread_edit = std::move(id);
    });
    mux::ui::show(*showing, mux::ui::thread_compose{std::move(said)});
  }
  // A message of the thread open answered there, as tdesktop's "Reply to
  // <name>" over the field.
  void answer_in_thread(std::string id, mux::ui::compose_context said) const {
    mux::ui::change_shown<mux::ui::chat_shown>(*showing, [&](mux::ui::chat_shown& now) {
      now.thread_answer = std::move(id);
      now.thread_edit.reset();
    });
    mux::ui::show(*showing, mux::ui::thread_compose{std::move(said)});
  }
  // The stretch of a chat's messages in view and a page around it -- or,
  // while a message is jumped to, a few around that: what is loaded on the
  // way is not looked at.
  [[nodiscard]] std::pair<std::size_t, std::size_t> list_around(const mux::conversation& chat, std::size_t around) const {
    const auto place = [&](const std::optional<std::string>& id) -> std::optional<std::size_t> {
      if (!id)
        return std::nullopt;
      const auto at = std::ranges::find(chat.timeline, *id, &mux::message::id);
      return at == chat.timeline.end() ? std::nullopt : std::optional(static_cast<std::size_t>(at - chat.timeline.begin()));
    };
    const std::size_t count = chat.timeline.size();
    if (list.jump_target) {
      const auto at = place(list.jump_target);
      return at ? std::pair{*at > 5 ? *at - 5 : 0, std::min(count, *at + 6)} : std::pair<std::size_t, std::size_t>{0, 0};
    }
    const std::size_t top = place(list.top).value_or(count > around ? count - around : 0);
    const std::size_t bottom = place(list.bottom).value_or(count == 0 ? 0 : count - 1);
    return {top > around ? top - around : 0, std::min(count, bottom + around + 1)};
  }
  std::optional<mux::logic::link_t> joining;
  // The settings as saved -- an account's privacy, its proxy, by its
  // address -- the program's.
  kept_settings* kept = nullptr;
  mux::vault::vault* vault = nullptr;
  // Work off the UI's thread.
  workers* work = nullptr;
  skiff::platform::dialogs::dialogs* system_dialogs = nullptr;
  // What wakes the window from another thread.
  wake_window* wake = nullptr;
  // What plays voice messages.
  mux::platform::audio::speaker* speaker = nullptr;

  [[nodiscard]] window_type& root() const { return scene->root(); }
  // What is shown, as the program says it: the chat chosen, the account
  // listed, whether the window is one pane at a time, the thread open.
  [[nodiscard]] const std::optional<conversation_id>& chosen() const { return showing->look<mux::ui::chat_shown>()->chosen; }
  [[nodiscard]] const std::optional<account_id>& listed() const { return showing->look<mux::ui::list_shown>()->account; }
  [[nodiscard]] bool single() const { return showing->look<mux::ui::screen_shown>()->single; }
  [[nodiscard]] std::optional<std::string> thread_open() const {
    const auto& now = *showing->look<mux::ui::chat_shown>();
    return now.threads_open ? now.thread : std::nullopt;
  }
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
  // The keys given to a field: what is shown says which, and asks again with
  // a count raised -- the field takes them as it reads that.
  void ask_keys(mux::ui::keys_target which) const {
    spl::visit(spl::overloaded{[&](mux::ui::keys::chat) { written_last = mux::ui::request::writing::chat{}; },
                               [&](mux::ui::keys::thread) { written_last = mux::ui::request::writing::thread{}; },
                               [](const auto&) {}},
               which);
    mux::ui::change_shown<mux::ui::keys_asked>(*showing, [&](mux::ui::keys_asked& now) {
      now.which = std::move(which);
      ++now.count;
    });
  }
  // The field written in last, of the chat's and the thread's: where the keys
  // were last given, or what was last typed into.
  mutable mux::ui::request::writing_t written_last = mux::ui::request::writing::chat{};
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
  // Which settings page is open, as the shown model holds it.
  template <class Page> [[nodiscard]] bool on_settings_page() const {
    const auto& shown = *showing->look<std::optional<mux::ui::settings_facts>>();
    return shown && spl::visit(spl::overloaded{[](const Page&) { return true; },
                                             [](const auto&) { return false; }}, shown->page);
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
  // A text menu, at the press that asked for it; and let go of.
  void show_text_menu(std::vector<mux::ui::text_menu_item> items, std::optional<skiff::scene::text_menu::of_field> field = std::nullopt) const {
    mux::ui::show(*showing, std::optional(mux::ui::text_menu_at(std::move(items), std::move(field), last_press, scene->viewport())));
  }
  void close_text_menu() const { mux::ui::show<mux::ui::text_menu_facts>(*showing, std::nullopt); }
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
  // Whether the settings are open, as what is shown says.
  [[nodiscard]] bool settings_open() { return showing->look<std::optional<mux::ui::settings_facts>>()->has_value(); }
  // The demo: no network, and nothing kept.
  [[nodiscard]] bool demo() const { return ask->demo; }

  // The chosen account, and its accounts page, when they are up.
  template <class F>
  void with_chosen_account(F&& f) {
    const auto chosen = mux::ui::chosen_of(*showing->look<std::optional<mux::ui::panel_facts>>());
    if (!chosen)
      return;
    // A copy changed, and put back in the model where it changed; its page
    // shows it as what is shown is read again.
    kept->change_account(*chosen, [&](mux::config::account_t& account) { f(account); });
    refresh_due = true;
  }
  // The chat Manage is for -- a space, where its settings are open: what is
  // chosen there goes to it -- else the chat chosen.
  [[nodiscard]] std::optional<mux::conversation_id> managed() const {
    if (managing)
      return managing;
    return this->chosen();
  }
  // Features belong to the selected account. An unsupported feature never
  // borrows another account's protocol or credentials.
  template <class Feature>
  [[nodiscard]] std::optional<mux::account_id> account_offering(Feature wanted) const {
    const auto current = this->listed();
    return current && mux::proto::offers(mux::ui::protocol_state_of(ui, *current), wanted)
        ? current : std::nullopt;
  }
};

}  // namespace mux::app
