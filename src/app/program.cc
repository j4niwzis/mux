// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.program: what the program does to the window between events --
// the class, its state and what it does, declared; what it does is defined
// in the program_*.cc beside this, a part each.
export module mux.app.program;

import std;
import splice;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
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
import mux.logic.links;

export namespace mux::app {

// What the program does to the window between events.
// What the program does to the window between events -- on what the
// accounts file keeps, its base.
struct app : kept_settings {
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
  template <class Request>
  void route(const Request& one) {
    static_assert(takes<search_part, Request> || takes<pictures_part, Request> || takes<reading_part, Request> || takes<outbox_part, Request> || takes<settings_part, Request> || takes<menu_part, Request> ||
                      takes<app, Request>, "a request no part of the program takes");
    if (!offer(search, one) && !offer(pictures, one) && !offer(reading, one) && !offer(outbox, one) &&
        !offer(settings, one) && !offer(menu, one))
      offer(*this, one);
  }

  using adding = mux::ui::add_account_pane<actions>;
  using accounts = mux::ui::accounts_panel<actions>;
  using xmpp_form = mux::ui::xmpp_form<actions>;
  using matrix_form = mux::ui::matrix_form<actions>;

  mailbox_type* box = nullptr;
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

  // -- messages on disk: every change to one written as it is now
  message_store store;
  void keep_on_disk(const mux::change_t& one);

  std::set<mux::conversation_id> members_fetched;

  // A Matrix session given: kept with its account, for the next start.
  void keep_session(const mux::change::session_given& given);

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
  [[nodiscard]] mux::ui::xmpp_form<actions>* xmpp_form_up();

  // Everything brought up to date with the model: each panel by its own
  // overload.
  // `from`: who asked -- said on stderr where MUX_TRACE_FRAMES is set, to
  // find what rebuilds the window when nothing should.
  void refresh(std::source_location from = std::source_location::current());
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
  void apply(const request::back&);
  void apply(const request::open_accounts&);
  void apply(const request::open_new_account&);
  void apply(const request::add_xmpp&);
  void apply(const request::add_matrix&);
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
  std::optional<mux::logic::link::room> joining;
  // A room not joined, looked up from a link: its card up, until it is
  // joined from there or closed.
  struct room_looked_up {
    mux::logic::link_step::join step;
    std::optional<mux::logic::link::room> link;
  };
  std::optional<room_looked_up> previewing;
  void follow(const mux::logic::link_t& where);
  void open_chat(const mux::conversation_id& which, const std::optional<std::string>& event);
  void go_to_message(const mux::conversation_id& in, std::string id, std::optional<std::string> fragment);
  // Older messages of a chat: from the disk while it has some from before
  // the oldest in memory, from the server past that.
  void apply(const request::load_older& one);
  // A window around a message jumped to, and a window paged forward.
  void apply(const request::load_context& one);
  void apply(const request::load_newer& one);
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
  void apply(const request::jump_to_mark& one);
  void go_to_marked(const mux::conversation& chat, mux::mark_kind_t kind, const std::string& event, const std::string& target);
  // The mentions and reactions not yet seen, written as they change and read
  // back at the start -- each put in once its chat is there.
  void save_marks();
  void load_marks();
  std::vector<std::pair<mux::conversation_id, mux::change_t>> pending_marks;
  // The marks read back of chats not here yet, as the file had them: written
  // again as they were until their chats come, not dropped by a save before.
  std::map<mux::conversation_id, mux::config::chat_marks> marks_not_here;
  void apply(const request::list_marks& one);
  void apply(const request::go_to_mark& one);
  void apply(const request::close_marks&);
  void apply(const request::open_explore&);
  void apply(const request::close_explore&);
  void apply(const request::search_rooms& one);
  void apply(const request::explore_space& one);
  void apply(const request::join_directory_room& one);
  void apply(const request::create_room& one);
  // The Matrix account rooms are found and made by: the one in view, else
  // the first.
  [[nodiscard]] std::optional<mux::account_id> matrix_account();
  void apply(const request::settings_notifications&);
  void apply(const request::flip_notify& one);
  void apply(const request::set_notify_backend& one);
  void apply(const request::flip_account_notify&);
  void apply(const request::flip_account_notify_sound&);
  void apply(const request::set_chat_notify& one);
  // Whether the window has the keyboard's focus: a message to the chat
  // being read then notifies nothing.
  bool window_focused = true;
  // The notifications mux shows itself, for the host to put up.
  struct toast_due {
    mux::conversation_id chat;
    std::string key;
    std::string title;
    std::string text;
  };
  using toast_card = mux::ui::toast_card;
  std::vector<toast_due> toasts_due;
  // When mux started: what was said before it is caught up on, not notified.
  std::chrono::sys_time<std::chrono::milliseconds> started_at =
      std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
  void open_notified(const mux::conversation_id& chat) { this->open_chat(chat, std::nullopt); }
  void focus_changed(bool on) { window_focused = on; }
  // A message come as it happened, notified as the settings say.
  void notify_of(const mux::message& said, bool mentions_me);
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
  // An invite come: said, as a message is -- once a run.
  void notify_invite(const mux::conversation_id& in, const mux::invite_info& invite, const std::string& name);
  std::set<mux::conversation_id> invites_told;
  void apply(const request::toggle_emoji&);
  void apply(const request::toggle_thread_emoji&);
  void open_emoji_at(float right, float top);
  // Which field the emoji picker writes in, as its button opened it.
  request::writing_t emoji_into_ = request::writing::chat{};
  void apply(const request::close_emoji&);
  void apply(const request::insert_emoji& one);
  void apply(const request::open_manage&);
  void apply(const request::explore_state&);
  void apply(const request::open_send_custom&);
  void apply(const request::close_devtools&);
  void apply(const request::send_custom& one);
  void apply(const request::open_new_chat&);
  void apply(const request::find_people& one);
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
  void apply(const request::set_jump_search&);
  void apply(const request::flip_chat_room_events&);
  void apply(const request::close_manage&);
  void apply(const request::room_act& one);
  void apply(const request::resize_info& one);
  void apply(const request::choose_new_proxy& one);
  // The chosen chat muted, or not: kept in the file.
  void apply(const request::toggle_mute&);
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
  void apply(const request::sign_out_sessions& one);
  void apply(const request::rename_session& one);
  void apply(const request::refresh_sessions&);
  // The account's id, as the model knows it, of a saved one.
  [[nodiscard]] static mux::account_id id_of(const mux::config::account_t& account) {
    const std::string address = mux::config::address_of(account);
    return mux::account_id{mux::ui::protocol_of(address), address};
  }
  void apply(const request::flip_account_receipts&);
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

  void switch_form(void (adding::*to)());

  // A new account: saved, and started; the panel waits to hear how it went.
  template <class Form>
  void add(Form& form) {
    auto typed = form.account();
    if (!typed) {
      form.say(typed.error(), true);
      return;
    }
    mux::config::account_t account{std::move(*typed)};
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
    mux::config::account_t account{std::move(*typed)};
    const std::string address = mux::config::address_of(account);
    const std::string was = form.editing.value_or(address);
    const auto old = this->find(was);
    if (old == saved.end())
      return;
    if (address != was && this->find(address) != saved.end()) {
      form.say("That account is already here.", true);
      return;
    }
    // What the form does not show is kept: on or off, the proxy, receipts.
    mux::config::enabled_of(account) = mux::config::enabled_of(*old);
    mux::config::proxy_in(account) = mux::config::proxy_of(*old);
    mux::config::read_receipts_in(account) = mux::config::read_receipts_in(*old);
    mux::config::send_typing_in(account) = mux::config::send_typing_in(*old);
    // And a Matrix session: the same user on the same homeserver goes on
    // with the device it has, rather than logging in as a new one at every
    // Save.
    splice::visit(splice::overloaded{[](mux::config::matrix_account& now, const mux::config::matrix_account& before) {
                                 if (before.user_id == now.user_id && before.homeserver == now.homeserver &&
                                     before.password == now.password) {
                                   now.access_token = before.access_token;
                                   now.device_id = before.device_id;
                                 }
                               },
                               [](auto&, const auto&) {}},
               account, std::as_const(*old));
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
