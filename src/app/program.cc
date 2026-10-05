// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.program: what the program does to the window between events --
// the class, its state and what it does, declared; what it does is defined
// in the program_*.cc beside this, a part each.
export module mux.app.program;

import std;
import mux.vault;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.media;
import mux.platform.audio;
import mux.platform.dialogs;
import mux.platform.events;
import mux.platform.notifications;
import mux.platform.push;
import mux.platform.system;
import mux.protocols;
import mux.ui;
import mux.ui.proto;
import mux.app.proto;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.workers;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.app.services;
import mux.app.kept;
import mux.app.search;
import mux.app.pictures;
import mux.app.drafts;
import mux.app.reading;
import mux.app.outbox;
import mux.app.settings;
import mux.app.menu;
import mux.app.notices;
import mux.app.marks;
import mux.app.history;
import mux.app.verification;
import mux.app.proxies;
import mux.app.packs;
import mux.app.rooms;
import mux.app.room_card;
import mux.app.preferences;
import mux.app.manage;
import mux.app.looks;
import mux.app.threads;
import mux.app.emoji;
import mux.app.accounts;
import mux.app.local_data;
import mux.app.calls;
import mux.logic.links;

export namespace mux::app {

// What the program does to the window between events.
// What the program does to the window between events -- on what the
// accounts file keeps, its base.
struct app : kept_settings {
  // Made in the theme's colours, which are in place before the window is.
  // Made in the theme's colours and the window's look as the program
  // starts: the window, made here, is shown in them.
  app(const mux::ui::palette& theme_colours, const mux::ui::window_look_t& window)
      : shared{.looks = {.window = window}}, colours(theme_colours) {
    shared.paint.looks = &shared.looks;
    shared.paint.colours = &colours;
  }
  // -- the parts: each owns its state, and reaches the rest through what
  // they share
  services shared;
  search_part search{shared};
  pictures_part pictures{shared};
  drafts_part drafts{shared};
  reading_part reading{shared};
  outbox_part outbox{shared, drafts, sending};
  menu_part menu{shared, outbox, pictures};
  settings_part settings{shared, *this, pictures};
  notices_part notices{shared};
  marks_part marks{shared};
  history_part paging{shared};
  verification_part verification{shared};
  proxies_part proxying{shared, *this};
  packs_part packs{shared};
  rooms_part rooms{shared};
  room_card_part room_card{shared};
  preferences_part preferences{shared, *this, proxying};
  manage_part manage{shared, *this};
  looks_part looks{shared, *this};
  threads_part threads{shared};
  emoji_part emoji{shared};
  accounts_part accounts_screen{shared, *this};
  local_data_part local_data{shared, *this};
  calls_part calls{shared};
  // Work off the UI's thread: decoding pictures, reading the disk.
  workers work;
  // Files chosen in the dialog, or dropped on the window: to the outbox.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (packs.took_files(paths, dropped))
      return;
    if (looks.took_files(paths, dropped))
      return;
    outbox.files_given(std::move(paths), dropped);
  }
  // Where Save As… was asked to put what it saves: to the pictures part.
  void save_path_chosen(std::string path) { pictures.save_to(std::move(path)); }
  // What the parts share, pointed at the program's own: once the program
  // is given its model, network and mailbox.
  void wire();
  // A request, to the part that takes it -- the first with an apply for
  // it, as overload resolution finds -- and to the program's own where
  // none does.
  template <class Part, class Request>
    requires requires(Part& part, const Request& one) { part.apply(one); }
  static bool offer(Part& part, const Request& one) {
    part.apply(one);
    return true;
  }
  template <class Part, class Request>
  static bool offer(Part&, const Request&) {
    return false;
  }
  template <class Part, class Request>
  static constexpr bool takes = requires(Part& part, const Request& one) { part.apply(one); };
  // A protocol's own request: done as its program glue says (mux.app.proto).
  template <class Request>
    requires requires(app& self, const Request& one) { program_asked(self, one); }
  void route(const Request& one) {
    program_asked(*this, one);
  }
  template <class Request>
  void route(const Request& one) {
    static_assert(takes<search_part, Request> || takes<pictures_part, Request> || takes<reading_part, Request> || takes<outbox_part, Request> || takes<settings_part, Request> || takes<menu_part, Request> || takes<notices_part, Request> || takes<marks_part, Request> || takes<history_part, Request> || takes<verification_part, Request> || takes<proxies_part, Request> || takes<packs_part, Request> || takes<rooms_part, Request> || takes<room_card_part, Request> || takes<preferences_part, Request> || takes<manage_part, Request> || takes<looks_part, Request> || takes<threads_part, Request> || takes<emoji_part, Request> || takes<accounts_part, Request> || takes<local_data_part, Request> || takes<calls_part, Request> ||
                      takes<app, Request>, "a request no part of the program takes");
    if (!offer(search, one) && !offer(pictures, one) && !offer(reading, one) && !offer(outbox, one) &&
        !offer(settings, one) && !offer(menu, one) && !offer(notices, one) && !offer(marks, one) && !offer(paging, one) &&
        !offer(verification, one) && !offer(proxying, one) && !offer(packs, one) &&
        !offer(rooms, one) && !offer(room_card, one) &&
        !offer(preferences, one) && !offer(manage, one) &&
        !offer(looks, one) && !offer(threads, one) && !offer(emoji, one) &&
        !offer(accounts_screen, one) && !offer(local_data, one) && !offer(calls, one))
      offer(*this, one);
  }

  using adding = mux::ui::add_account_pane<actions>;
  using accounts = mux::ui::accounts_panel<actions>;

  mailbox_type* box = nullptr;
  // What wakes the window: as main made it, for the mailbox.
  wake_window wake;
  // The system's dialogs, put over the window as it is made.
  mux::platform::dialogs::dialogs system_dialogs;
  mux::model* model = nullptr;
  network* net = nullptr;
  // A room the user made, to be shown as soon as the model has it.
  std::optional<mux::conversation_id> made_room_;
  // What plays voice messages: handed to the window and to the parts.
  mux::platform::audio::speaker speaker;
  // The theme's colours: handed to the window, which is made in them --
  // made again in new ones when the theme changes (rebuild_in_theme).
  mux::ui::palette colours;
  actions ask;
  skiff::scene::Scene<window_type> scene{std::in_place,
                                         mux::ui::ui_needs<actions>{.actions = &ask, .sound = &speaker, .colours = &colours, .emoji = &shared.emoji, .looks = &shared.looks, .paint = &shared.paint, .shared = &shared.ui}};

  // -- what the host asks
  skiff::scene::Scene<window_type>& window();
  // What the host draws the window with.
  mux::ui::mux_paint& painting() { return shared.paint; }

  void woken();
  // A link's message gone to, and a thread's answer to be scrolled to once
  // its panel shows it.
  void go_to_linked(const mux::conversation_id& in, const std::string& event);
  bool open_in_thread(const mux::conversation& chat, const std::string& id);
  std::optional<std::pair<mux::conversation_id, std::string>> linked_;
  std::optional<std::string> thread_target_;

  // -- messages on disk: every change to one written as it is now
  message_store store;

  std::set<mux::conversation_id> members_fetched;

  // A Matrix session given: kept with its account, for the next start.

  // A link pressed in a message's text: routed as a link is.
  void open_link(std::string url) { ask.open_url(std::move(url)); }
  void before_frame();

  void closing();
  // Drafts: in the screen, and on disk in one small file, written anew
  // when one changes.

  // -- the window
  window_type& root();

  void show_conversations();



  // Everything brought up to date with the model: each panel by its own
  // overload.
  // `from`: who asked -- said on stderr where MUX_TRACE_FRAMES is set, to
  // find what rebuilds the window when nothing should.
  void refresh(std::source_location from = std::source_location::current());
  // Each thing the window shows of the settings, brought up to date.
  void note_spaces();
  void show_placements();
  void show_event_filters();
  void show_looks_now();
  void show_space_bars();
  void show_levels();
  void show_backgrounds();
  void show_chat_choices();
  // Every chat of every account.
  [[nodiscard]] auto all_chats() const {
    return model->accounts() | std::views::values |
           std::views::transform([](const auto& account) -> const auto& { return account.conversations; }) | std::views::join |
           std::views::values;
  }
  // What is kept, applied: the settings read at the start -- or, where local
  // data is encrypted, once it is unlocked -- and the accounts started.
  void begin(const mux::config::file& saved, std::vector<mux::config::account_t> extra, bool demo,
             std::optional<std::string> error);
  // Local data encrypted and locked: the unlock screen, and what the start
  // would have done kept until it is opened.
  void lock(std::vector<mux::config::account_t> extra, bool demo);

  void apply(const request::choose& one);

  // A chat opened: read up to its last message from someone else, and the
  // people in it told so where read receipts are on.
  // The user's own read position: kept here always -- in the model and on
  // disk -- and told to the server only where the account's privacy lets it.

  // The chosen chat left: a Matrix room here; XMPP rooms are not there yet.
  void apply(const request::leave_chat&);
  // Out of the chat open, back to the chats: what was written kept as its draft.
  void apply(const request::close_chat&);
  void apply(const request::back&);
  void apply(const request::open_drawer&);
  void apply(const request::quit&);
  void apply(const request::toggle_info&);
  void apply(const request::jump_to_end&);
  void apply(const request::return_to_chat&);
  void go_live(const mux::conversation_id& in);

  // A message's menu, and what is chosen from it.
  // -- files to send: chosen with the paperclip, or dropped on the window
  // Files given: read, a picture known by its bytes; a picture dropped on
  // the window written anew from its pixels -- nothing of its file, its
  // metadata among it, goes with it -- and named image.<its type>. Then the
  // send box, with what was waiting in it before.
  // Sent: each file, the caption with the first; the box closed.

  // A message swiped to the left: answered, as its menu's Reply does.
  // A reaction: the user's own put where it is not, taken back where it
  // is -- shown at once, and told to the server.
  // A picture or a file saved into Downloads, from the menu.
  // A link pressed: to a user, a room or a message, in here -- matrix.to,
  // matrix: and xmpp: links -- and anywhere else, in the browser.
  void apply(const request::open_url& one);

  // A link followed, as where it leads says: a chat opened -- and a message
  // in it jumped to -- a person's page, a word said, or a room joined, and
  // opened when it comes.
  void follow(const mux::logic::link_t& where);
  void open_chat(const mux::conversation_id& which, const std::optional<std::string>& event);
  void go_to_message(const mux::conversation_id& in, std::string id, std::optional<std::string> fragment);
  // Older messages of a chat: from the disk while it has some from before
  // the oldest in memory, from the server past that.
  // A window around a message jumped to, and a window paged forward.
  void apply(const request::resize_sidebar& one);
  // A member written to: their direct chat, where there is one already.
  void apply(const request::message_person& one);
  void apply(const request::jump_to_message& one);

  // A sender pressed in the messages: their page, in the chat's info.
  void apply(const request::open_member_info& one);
  void apply(const request::not_implemented& one);
  void apply(const request::close_notice&);
  void apply(const request::close_person_info&);
  // The person whose card is open, in which chat: shown again as what is
  // known of their keys comes.
  std::optional<std::pair<mux::conversation_id, std::string>> person_open_;
  // The notifications mux shows itself, for the host to put up; one pressed;
  // the window's focus -- the notices part's.
  using toast_due = notices_part::toast_due;
  using toast_card = mux::ui::toast_card;
  [[nodiscard]] std::vector<toast_due> take_toasts() { return notices.take_toasts(); }
  void open_notified(const mux::conversation_id& chat) { this->open_chat(chat, std::nullopt); }
  void focus_changed(bool on) { notices.focus_changed(on); }
  // Whether the window is on screen: not hidden, minimised, covered or
  // suspended -- a phone's screen off, another app over it. Off it, only
  // the connections go on: the model kept up and notifications given, and
  // nothing else -- the window not brought up to date, no pictures asked
  // for, nothing marked read, no frames. Brought up to date as it comes back.
  bool on_screen = true;
  bool refresh_waiting_ = false;
  void shown_changed(bool now) {
    if (now == on_screen)
      return;
    on_screen = now;
    if (now && std::exchange(refresh_waiting_, false))
      this->refresh();
  }
  void apply(const request::close_dialog&);
  void apply(const request::copy_text& one);
  void apply(const request::text_key& one);
  void apply(const request::give_passphrase&);
  // The account whose room keys a passphrase was asked for.
  std::optional<mux::account_id> keys_of;
  void apply(const request::resize_info& one);
  // ← on the accounts page: from an account's pages to the list, from the
  // list to the chats.
  void apply(const request::accounts_back&);
  void apply(const request::open_replacement&);
  // A limit halved or doubled, within its bounds: kept, and in force at once.
  // What is kept on disk, gone: the stored messages and the pictures, and
  // the pictures in memory, to be fetched again as they are wanted.
  // The theme or the renderer chosen: kept, for the next start.
  // A theme chosen: its colours in place, and the window made again in them,
  // as it was -- the chats, the one chosen, the widths -- with Settings open
  // where it was.
  // An accent chosen: the same as a theme, over it.
  // The theme and accent now chosen put in place, and the window made again
  // in them, as it was, with Settings open on Appearance.
  void rebuild_in_theme();


  // What is in the message field, to the chosen chat -- a new message, an
  // answer to one, or one edited -- and the field emptied.
  // Another account's chats listed: the drawer goes back, and no chat is
  // chosen.
  void apply(const request::switch_account& one);
  void apply(const request::pop_panel&);

  // How much moves, from now on and in the file.


};

}  // namespace mux::app
