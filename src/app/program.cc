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
import mux.logic.links;

export namespace mux::app {

// What the program does to the window between events.
// What the program does to the window between events -- on what the
// accounts file keeps, its base.
struct app : kept_settings {
  // Whether it is used by a finger -- a phone's screen -- as the host saw
  // last: a touch, or a mouse's press. Where it is, the input is not given
  // the keys' focus on its own: that started the text input, and with it a
  // phone's on-screen keyboard, over half the screen whenever a chat was
  // open. The keyboard comes up as the field is tapped, as on Telegram's.
  bool by_touch = false;
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
  history_part history{shared};
  // Work off the UI's thread: decoding pictures, reading the disk.
  workers work;
  // Files chosen in the dialog, or dropped on the window: to the outbox.
  void files_given(std::vector<std::string> paths, bool dropped) {
    if (std::exchange(picking_pack_images, false) && !dropped) {
      this->pack_files(paths);
      return;
    }
    if (picking_wallpaper && !dropped) {
      if (!paths.empty())
        this->wallpaper_file(paths.front());
      picking_wallpaper.reset();
      return;
    }
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
    static_assert(takes<search_part, Request> || takes<pictures_part, Request> || takes<reading_part, Request> || takes<outbox_part, Request> || takes<settings_part, Request> || takes<menu_part, Request> || takes<notices_part, Request> || takes<marks_part, Request> || takes<history_part, Request> ||
                      takes<app, Request>, "a request no part of the program takes");
    if (!offer(search, one) && !offer(pictures, one) && !offer(reading, one) && !offer(outbox, one) &&
        !offer(settings, one) && !offer(menu, one) && !offer(notices, one) && !offer(marks, one) && !offer(history, one))
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
  // The proxy chosen for the account being added, as it is added.
  std::optional<std::string> new_proxy;
  // What the message field's text is: a new message, an answer to one, or
  // one edited; and the message whose menu is up.
  // The drawer, left open under a page coming in over it, to go when the
  // page is in.
  bool drawer_waits = false;
  // A room the user made, to be shown as soon as the model has it.
  std::optional<mux::conversation_id> made_room_;
  // The account being added that a login is waiting to hear about.
  std::optional<std::string> pending_login;
  actions ask;
  skiff::scene::Scene<window_type> scene{std::in_place, &ask};

  // -- what the host asks
  skiff::scene::Scene<window_type>& window();

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
  accounts& show_accounts();
  // The accounts, with this one's settings up beside them.
  accounts& show_account(const std::string& address);
  // Adding an account: beside the list, on the accounts page.
  void show_adding();


  // The panel that is up, if one is, and the XMPP form in it, if there is one.
  [[nodiscard]] mux::ui::account_form<actions>* form_up();

  // Everything brought up to date with the model: each panel by its own
  // overload.
  // `from`: who asked -- said on stderr where MUX_TRACE_FRAMES is set, to
  // find what rebuilds the window when nothing should.
  void refresh(std::source_location from = std::source_location::current());
  // What is kept, applied: the settings read at the start -- or, where local
  // data is encrypted, once it is unlocked -- and the accounts started.
  void begin(const mux::config::file& saved, std::vector<mux::config::account_t> extra, bool demo,
             std::optional<std::string> error);
  // Local data encrypted and locked: the unlock screen, and what the start
  // would have done kept until it is opened.
  void lock(std::vector<mux::config::account_t> extra, bool demo);
  std::vector<mux::config::account_t> waiting_extra;
  bool waiting_demo = false;
  void bring_up_to_date(accounts& panel);

  // A new account waiting to log in: online is done, failed is said.
  template <class Form>
  void watch_login(Form& form) {
    if (!pending_login)
      return;
    const auto found = model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login), *pending_login});
    if (found == model->accounts().end())
      return;
    splice::visit(splice::overloaded{[&](const mux::connection::online&) { this->show_conversations(); },
                               [&](const mux::connection::failed& why) {
                                 form.say(why.error.empty() ? "The server said no." : why.error, true);
                                 pending_login.reset();
                               },
                               [&](const auto&) { form.say("Connecting…", false); }},
               found->second.state);
  }

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
  void apply(const request::open_accounts&);
  void apply(const request::open_new_account&);
  void apply(const request::add_account_of&);
  void apply(const request::select_account& one);
  void apply(const request::toggle_advanced&);
  void apply(const request::toggle_plain&);
  void apply(const request::submit_login&);
  void apply(const request::flip_enabled& one);
  void apply(const request::remove_account& one);
  void apply(const request::open_drawer&);
  void apply(const request::show_account& one);
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
  std::optional<mux::logic::link_t> joining;
  // A room not joined, looked up from a link: its card up, until it is
  // joined from there or closed.
  struct room_looked_up {
    mux::logic::link_step::join step;
    std::optional<mux::logic::link_t> link;
  };
  std::optional<room_looked_up> previewing;
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
  void apply(const request::close_room_card&);
  // The person whose card is open, in which chat: shown again as what is
  // known of their keys comes.
  std::optional<std::pair<mux::conversation_id, std::string>> person_open_;
  void apply(const request::open_explore&);
  void apply(const request::close_explore&);
  void apply(const request::search_rooms& one);
  void apply(const request::explore_space& one);
  void apply(const request::join_directory_room& one);
  void apply(const request::create_room& one);
  // The Matrix account rooms are found and made by: the one in view, else
  // the first.
  // An account whose protocol offers what is asked: the one in view where
  // it does, else the first.
  template <class Feature>
  [[nodiscard]] std::optional<mux::account_id> account_offering(Feature wanted) {
    if (const auto& current = root().main().current; current && mux::proto::offers(mux::ui::protocol_state_of(*current), wanted))
      return current;
    for (const auto& [id, account] : model->accounts())
      if (mux::proto::offers(mux::ui::protocol_state_of(id), wanted))
        return id;
    return std::nullopt;
  }
  void apply(const request::flip_account_notify&);
  void apply(const request::flip_account_notify_sound&);
  void apply(const request::set_chat_notify& one);
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
  void apply(const request::set_room_event_kind& one);
  void apply(const request::set_room_events& one);
  void apply(const request::manage_space& one);
  void apply(const request::flip_forum& one);
  void apply(const request::flip_home_hide& one);
  void apply(const request::close_forum&);
  void apply(const request::manage_forum&);
  // The chat Manage is for: a space, where its settings are open -- what is
  // chosen there goes to it -- else the chat chosen.
  std::optional<mux::conversation_id> manage_target;
  [[nodiscard]] std::optional<mux::conversation_id> managed() {
    return manage_target && root().manage_up() ? manage_target : root().main().chosen;
  }
  void manage_chat(const mux::conversation_id& id);
  void apply(const request::place_spaces& one);
  void apply(const request::set_space_bars& one);
  void apply(const request::set_home_hides& one);
  void apply(const request::set_home_direct& one);
  void apply(const request::join_room_card&);
  void apply(const request::knock_room_card&);
  void apply(const request::decline_room_card&);
  void apply(const request::toggle_emoji&);
  void apply(const request::toggle_thread_emoji&);
  void open_emoji_at(float right, float top);
  // Which field the emoji picker writes in, as its button opened it.
  request::writing_t emoji_into_ = request::writing::chat{};
  void apply(const request::close_emoji&);
  void apply(const request::insert_emoji& one);
  void apply(const request::open_manage&);
  void apply(const request::close_dialog&);
  void apply(const request::open_new_chat&);
  void apply(const request::find_people& one);
  void apply(const request::search_elsewhere& one);
  void apply(const request::open_new_room&);
  void apply(const request::open_wallpaper& one);
  void apply(const request::close_wallpaper&);
  void apply(const request::set_wallpaper& one);
  void apply(const request::set_bubbles& one);
  // A picture being chosen for a background: at which level.
  std::optional<mux::choice_level_t> picking_wallpaper;
  void wallpaper_file(const std::string& path);
  void apply(const request::toggle_threads&);
  void apply(const request::open_thread& one);
  void apply(const request::close_thread&);
  void apply(const request::send_in_thread& one);
  void apply(const request::open_packs&);
  void apply(const request::open_room_packs&);
  void apply(const request::close_packs&);
  void apply(const request::save_pack& one);
  void apply(const request::delete_pack& one);
  void apply(const request::pick_pack_images&);
  // The packs' dialog: the account whose packs it shows, and whether the
  // files chosen next are its images.
  std::optional<mux::account_id> packs_account;
  bool picking_pack_images = false;
  void pack_files(const std::vector<std::string>& paths);
  void apply(const request::close_new_room&);
  void apply(const request::copy_text& one);
  void apply(const request::close_new_chat&);
  void apply(const request::start_direct& one);
  void apply(const request::start_group& one);
  void apply(const request::flip_account_room_events&);
  void apply(const request::set_receipts_shown&);
  void apply(const request::set_link_previews&);
  void apply(const request::set_typing_sent&);
  void apply(const request::set_previews_direct&);
  void apply(const request::give_passphrase&);
  void apply(const request::verify_person&);
  void apply(const request::verify_accept_now&);
  void apply(const request::verify_cancel_now&);
  void apply(const request::verify_match&);
  void apply(const request::verify_mismatch&);
  void apply(const request::close_verification&);
  // The verification its dialog shows: its account, and its transaction.
  std::optional<std::pair<mux::account_id, std::string>> verifying;
  // The account whose room keys a passphrase was asked for.
  std::optional<mux::account_id> keys_of;
  void apply(const request::flip_local_encryption&);
  void apply(const request::change_passphrase&);
  // The files local data's encryption seals: the settings, and what is kept
  // in the state directory through the vault.
  [[nodiscard]] mux::vault::vault::kept_files sealed_files() const;
  // Everything kept sealed again as the vault is after `turn` -- on, off, or
  // under another passphrase; false, and nothing changed, where it could
  // not all be read.
  template <class Turn>
  [[nodiscard]] bool reseal(Turn turn);
  void apply(const request::set_jump_search&);
  void apply(const request::flip_chat_room_events&);
  void apply(const request::close_manage&);
  void apply(const request::room_act& one);
  void apply(const request::resize_info& one);
  void apply(const request::choose_new_proxy& one);
  // The chosen chat muted, or not: kept in the file.
  void apply(const request::toggle_mute&);
  void apply(const request::toggle_mute_of& one);
  void apply(const request::close_account_pages&);
  // ← on the accounts page: from an account's pages to the list, from the
  // list to the chats.
  void apply(const request::accounts_back&);
  // The chosen account, and its accounts page, when they are up.
  template <class F>
  void with_chosen_account(F&& f) {
    auto* up = root().open_panel();
    if (!up)
      return;
    splice::visit(
        [&](accounts& panel) {
          if (!panel.selected)
            return;
          if (const auto found = this->find(*panel.selected); found != saved.end())
            f(panel, *found);
        },
        *up);
  }
  void apply(const request::account_page& one);
  // The account's id, as the model knows it, of a saved one.
  [[nodiscard]] static mux::account_id id_of(const mux::config::account_t& account) {
    const std::string address = mux::config::address_of(account);
    return mux::account_id{mux::ui::protocol_of(address), address};
  }
  void apply(const request::flip_account_receipts&);
  void apply(const request::flip_only_verified&);
  void apply(const request::accept_identity& one);
  void apply(const request::set_account_colour& one);
  void apply(const request::flip_account_strip&);
  void apply(const request::open_replacement&);
  void apply(const request::place_chat& one);
  void apply(const request::unplace_chat& one);
  void apply(const request::flip_chat_strip& one);
  void apply(const request::set_chat_strip_colour& one);
  // A chat's placement in a list, where it has one.
  mux::config::chat_placement* placement_of(const mux::conversation_id& chat, const mux::account_id& in);
  void apply(const request::proxy_kind& one);
  // The chosen account through a profile, or none: kept, and connected again.
  void apply(const request::choose_account_proxy& one);
  void reconnect(const mux::config::account_t& account);
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name);
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
  void apply(const request::manage_proxies&);
  void apply(const request::settings_proxies&);
  void apply(const request::add_proxy&);
  void apply(const request::edit_proxy& one);
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it.
  void apply(const request::save_proxy_profile&);
  // A profile deleted: the accounts that used it connect directly.
  void apply(const request::delete_proxy_profile&);


  // What is in the message field, to the chosen chat -- a new message, an
  // answer to one, or one edited -- and the field emptied.
  // Another account's chats listed: the drawer goes back, and no chat is
  // chosen.
  void apply(const request::switch_account& one);
  void apply(const request::pop_panel&);

  // How much moves, from now on and in the file.

  void switch_form(const mux::protocol_t& speaks);

  // A new account: saved, and started; the panel waits to hear how it went.
  template <class Form>
  void add(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{.own = mux::config::kept_t{std::move(*typed)}};
    mux::config::proxy_in(account) = std::exchange(new_proxy, std::nullopt);
    const std::string address = mux::config::address_of(account);
    if (this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    saved.push_back(account);
    if (auto failed = this->write()) {
      form.say(*failed, true);
      return;
    }
    net->add(account, proxies);
    pending_login = address;
    form.say("Connecting…", false);
  }

  // An account's settings changed: the old one stops, and the new one, on
  // or off as the old one was, takes its place in the list.
  template <class Form>
  void edit(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{.own = mux::config::kept_t{std::move(*typed)}};
    const std::string address = mux::config::address_of(account);
    const std::string was = form.editing.value_or(address);
    const auto old = this->find(was);
    if (old == saved.end())
      return;
    if (address != was && this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    // What the form does not show is kept: every setting every account has
    // -- on or off, the proxy, receipts, its colour and look. (Only four of
    // them were, and an edit dropped the rest.)
    account.shared = old->shared;
    // And what its protocol keeps through an edit (a Matrix session: the
    // device it has, not a new one at every Save).
    splice::visit([](auto& now, const auto& before) {
                    using mux::proto::kept_defaults::carry_over;
                    carry_over(now, before);
                  },
                  account.own, std::as_const(old->own));
    // Nothing changed: saved as it is, and the connection left alone.
    const bool same = account == *old;
    *old = account;
    const auto failed = this->write();
    if (!same) {
      net->remove(was);
      if (mux::config::enabled_of(account))
        net->add(account, proxies);
    }
    // The form is rebuilt from what was saved: `form` is gone after this.
    auto& panel = this->show_account(address);
    if (auto* editor = panel.editor())
      editor->say(failed ? *failed : std::string("Saved."), failed.has_value());
  }

  void flip_enabled(const std::string& address);

  void remove(const std::string& address);

  // Written, and what went wrong said on the accounts page.
  void save_from_accounts();
};

}  // namespace mux::app
