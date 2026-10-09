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
import skiff.bind;
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

// What a press is answered with where it is made -- in a release build, as
// the window routes it, every node with its type: the settings model the
// window's scopes edit, and the program's sink for what nothing in the
// window takes. The program says them once it has them; till then nothing
// is answered there, and a press is delivered along its path instead.
struct app;
struct press_target {
  kept_model* model = nullptr;
  app* sink = nullptr;
};
inline press_target& press_target_now() {
  static press_target kept;
  return kept;
}
// What the window's routing starts with (found by its root's type).
inline auto startCarry(window_type&) {
  const press_target& now = press_target_now();
  return skiff::bind::carryFrom(now.model, now.sink);
}

// What the program does to the window between events.
// What the program does to the window between events -- on what the
// accounts file keeps, its base.
struct app : kept_settings {
  // Made in the theme's colours, which are in place before the window is.
  // Made in the theme's colours and the window's look as the program
  // starts: the window, made here, is shown in them.
  app(const mux::ui::palette& theme_colours, const mux::ui::window_look_t& window);
  // Keep construction, exception cleanup and UI teardown in the owning
  // module unit; importers must not instantiate the window's destructors.
  ~app();
  // -- the parts: each owns its state, and reaches the rest through what
  // they share
  services shared;
  search_part search{shared};
  pictures_part pictures{shared};
  drafts_part drafts{shared};
  reading_part reading{shared};
  outbox_part outbox{shared, drafts, this->sending()};
  menu_part menu{&shared, &outbox, &pictures};
  settings_part settings{shared, *this, pictures};
  notices_part notices{shared};
  marks_part marks{shared};
  history_part paging{shared};
  verification_part verification{shared};
  proxies_part proxying{shared, *this};
  packs_part packs{&shared};
  rooms_part rooms{shared};
  room_card_part room_card{shared};
  preferences_part preferences{shared, *this, proxying};
  manage_part manage{shared, *this};
  looks_part looks{shared, *this};
  threads_part threads{shared};
  emoji_part emoji{&shared};
  accounts_part accounts_screen{shared, *this};
  local_data_part local_data{shared, *this};
  calls_part calls{shared};
  // Work off the UI's thread: decoding pictures, reading the disk.
  workers work;
  // Files chosen in the dialog, or dropped on the window: to the outbox.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (took_files(packs, paths, dropped))
      return;
    if (looks.took_files(paths, dropped))
      return;
    outbox.files_given(std::move(paths), dropped);
  }
  // Where Save As… was asked to put what it saves: to the pictures part.
  void save_path_chosen(std::string path) { pictures.save_to(std::move(path)); }
  // The file dialog not shown -- on Linux, SDL asks xdg-desktop-portal for
  // it, else zenity: with neither, the paperclip did nothing at all.
  void dialog_failed(std::string why) {
    shared.notice("The file dialog could not be opened",
                        "The system gave no file dialog (" + why +
                            "). On Linux it comes from xdg-desktop-portal with a backend (-gtk, -gnome, -kde or -wlr), or "
                            "from zenity: install one of them. Meanwhile files can be dropped on the window, and pictures pasted.");
  }
  // What the parts share, pointed at the program's own: once the program
  // is given its model, network and mailbox.
  void wire();
  // A request, to the part that takes it -- the first with an apply for
  // it, as overload resolution finds -- and to the program's own where
  // none does.
  // Offered to a part: done by it -- and what it asks for in turn, where
  // its apply() returns that, done too.
  template <class Part, class Request>
    requires requires(Part& part, const Request& one) {
      { part.apply(one) } -> std::same_as<void>;
    }
  bool offer(Part& part, const Request& one) {
    part.apply(one);
    return true;
  }
  template <class Part, class Request>
    requires requires(Part& part, const Request& one) { part_apply(part, one); }
  bool offer(Part& part, const Request& one) {
    part_apply(part, one);
    return true;
  }
  // What a part asks for in turn, in its own type, given to the program
  // to do at once: where the part decides it in a visit -- a chat's
  // protocol -- its type is static there.
  struct taker {
    app* program = nullptr;
    template <class R>
    void operator()(const R& asked) const {
      program->take(asked);
    }
  };
  template <class Part, class Request>
    requires requires(Part& part, const Request& one, const taker& asked) { part.apply(one, asked); }
  bool offer(Part& part, const Request& one) {
    part.apply(one, taker{this});
    return true;
  }
  template <class Part, class Request>
  bool offer(Part&, const Request&) {
    return false;
  }

 public:
  // What the window answers, and what the program's parts ask in turn:
  // done at once, as it is taken -- the program is the window's sink. A
  // press is answered after the handler that made it has returned, and
  // what is done to the window is a model's edit, read by the bindings
  // after the event: nothing pressed is gone under its own answer.
  template <class E>
  void take(const E& one) {
    this->route(one);
  }
  template <class... E>
  void take(const spl::variant<E...>& one) {
    spl::visit([this](const auto& each) { this->take(each); }, one);
  }
  template <class E>
  void take(const std::optional<E>& one) {
    if (one)
      this->take(*one);
  }
  // A protocol's "nothing to ask": nothing.
  void take(const mux::proto::part::no_request&) {}
  template <class Part, class Request>
  static constexpr bool takes = requires(Part& part, const Request& one) { part_apply(part, one); } ||
                                requires(Part& part, const Request& one) { part.apply(one); } ||
                                requires(Part& part, const Request& one, const taker& asked) { part.apply(one, asked); };
  // A protocol's own request: done as its program glue says (mux.app.proto).
  template <class Request>
    requires requires(app& self, const Request& one) { program_asked(self, one); }
  void route(const Request& one) {
    program_asked(*this, one);
  }
  // The program's parts, in the order a request is offered to them; what
  // none of them takes, the program's own.
  auto parts() {
    return std::tie(search, pictures, reading, outbox, settings, menu, notices, marks, paging, verification, proxying,
                    packs, rooms, room_card, preferences, manage, looks, threads, emoji, accounts_screen, local_data, calls);
  }
  template <class Request>
  void route(const Request& one) {
    std::apply(
        [&](auto&... part) {
          static_assert((takes<std::remove_reference_t<decltype(part)>, Request> || ...) || takes<app, Request>,
                        "a request no part of the program takes");
          (void)((offer(part, one) || ...) || offer(*this, one));
        },
        parts());
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
  window_scene scene_storage{mux::ui::ui_needs<actions>{.sound = &speaker, .colours = &colours, .emoji = &shared.emoji,
      .looks = &shared.looks, .paint = &shared.paint, .shared = &shared.ui}};
  skiff::scene::Scene<window_type>& scene = scene_storage.get();

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
  void open_link(std::string url) { this->take(mux::ui::request::open_url{std::move(url)}); }
  void before_frame();
  [[nodiscard]] double wake_in() const;
  void after_event();
  // The window bound to the model, and the pages with the model's widgets
  // it was last walked whole with.
  skiff::bind::Binding<kept_model> window_binding;
  // What the window shows that the program opens and closes -- the dialogs'
  // facts -- and the binding the dialogs read it through.
  mux::ui::shown_model showing{mux::ui::shown_root{}};
  skiff::bind::Binding<mux::ui::shown_model> showing_binding;
  // What is shown read by the dialogs and layers bound to it; a menu just
  // made, given the keys.
  void refresh_shown();
  std::array<skiff::scene::NodeId, 3> bound_pages{};
  // And to the chats, the window's other model.
  skiff::bind::Binding<mux::chats_model> chats_binding;
  // What the window wants of the program as its state comes to want it.
  mux::ui::wants_model wants;
  void take_wants();
  void show_chats_now();
  void take_page_input();
  void settle_model();
  void show_looks();

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
  // Every chat of every account.
  [[nodiscard]] auto all_chats() const {
    return std::views::values(std::views::join(std::views::transform(std::views::values(model->accounts()), [](const auto& account) -> const auto& { return account.conversations; })));
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
  void apply(const request::open_leave_space&);
  void apply(const request::leave_space&);
  void apply(const request::close_leave_space&);
  // Out of the chat open, back to the chats: what was written kept as its draft.
  void apply(const request::close_chat&);
  void apply(const request::back&);
  void apply(const request::open_drawer&);
  void apply(const request::close_drawer&);
  void apply(const request::banner_pressed&);
  void apply(const request::card_action&);
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
  using toast_card = mux::ui::toast_card_t;
  static auto make_toast(const mux::ui::palette& colours, std::string key, std::string title, std::string text) {
    return mux::ui::toast_card(colours, std::move(key), std::move(title), std::move(text));
  }
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
  void apply(const request::text_formatting& one);
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
