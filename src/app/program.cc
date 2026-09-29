// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.program: what the program does to the window between events --
// the class, its state and what it does, declared; what it does is defined
// in the program_*.cc beside this, a part each.
export module mux.app.program;

import std;
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
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;
import mux.app.services;
import mux.app.search;
import mux.app.pictures;

export namespace mux::app {

// What the program does to the window between events.
struct app {
  // -- the parts: each owns its state, and reaches the rest through what
  // they share
  services shared;
  search_part search{shared};
  pictures_part pictures{shared};
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
    static_assert(takes<search_part, Request> || takes<pictures_part, Request> || takes<app, Request>, "a request no part of the program takes");
    if (!offer(search, one) && !offer(pictures, one))
      offer(*this, one);
  }

  using adding = mux::ui::add_account_pane<actions>;
  using accounts = mux::ui::accounts_panel<actions>;
  using xmpp_form = mux::ui::xmpp_form<actions>;
  using matrix_form = mux::ui::matrix_form<actions>;

  mailbox_type* box = nullptr;
  mux::model* model = nullptr;
  network* net = nullptr;
  std::filesystem::path config_path;
  std::vector<mux::config::account_t> saved;
  // How much moves, as read, to be written back as it was.
  std::optional<std::string> motion;
  // The theme and the renderer, for the next start: kept in the file.
  mux::config::theme_t theme = mux::config::theme::tinted{};
  mux::config::accent_t accent = mux::config::accent::theme_own{};
  mux::config::renderer_t renderer = mux::config::renderer::opengl{};
  // How much is kept, in memory and on disk.
  mux::config::cache_limits limits;
  // What is done to a picture dropped before it is sent.
  mux::config::sending_settings sending;
  void apply_limits();
  // The proxy chosen for the account being added, as it is added.
  std::optional<std::string> new_proxy;
  // What the message field's text is: a new message, an answer to one, or
  // one edited; and the message whose menu is up.
  compose_t composing = compose::plain{};
  request::message_menu menu_target;
  // The chats muted, and the proxy profiles: kept in the file.
  std::set<mux::conversation_id> muted;
  std::vector<mux::config::proxy_settings> proxies;
  // Why the accounts file could not be read, when it could not: then it is
  // not written over either.
  std::optional<std::string> config_error;
  // The drawer, left open under a page coming in over it, to go when the
  // page is in.
  bool drawer_waits = false;
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

  void before_frame();

  void closing();
  // Drafts: in the screen, and on disk in one small file, written anew
  // when one changes.
  void keep_draft(const mux::conversation_id& in, const std::string& text);
  void load_drafts();

  // -- the window
  window_type& root();

  void show_conversations();
  accounts& show_accounts();
  // The accounts, with this one's settings up beside them.
  accounts& show_account(const std::string& address);
  // Adding an account: beside the list, on the accounts page.
  void show_adding();

  std::vector<mux::config::account_t>::iterator find(std::string_view address);

  // The panel that is up, if one is, and the XMPP form in it, if there is one.
  [[nodiscard]] mux::ui::xmpp_form<actions>* xmpp_form_up();

  // Everything brought up to date with the model: each panel by its own
  // overload.
  void refresh();
  void bring_up_to_date(accounts& panel);

  // A new account waiting to log in: online is done, failed is said.
  template <class Form>
  void watch_login(Form& form) {
    if (!pending_login)
      return;
    const auto found = model->accounts().find(mux::account_id{mux::ui::protocol_of(*pending_login), *pending_login});
    if (found == model->accounts().end())
      return;
    std::visit(mux::overloaded{[&](const mux::connection::online&) { this->show_conversations(); },
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
  void mark_read(const mux::conversation_id& which);

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
  void apply(const request::set_motion& one);
  void apply(const request::quit&);
  void apply(const request::open_settings&);
  void apply(const request::close_settings&);
  void apply(const request::toggle_info&);
  void apply(const request::jump_to_end&);

  // A message's menu, and what is chosen from it.
  void apply(const request::message_menu& one);
  void apply(const request::close_menu&);
  // -- files to send: chosen with the paperclip, or dropped on the window
  struct prepared_file {
    std::string bytes;
    std::string name;
    std::string mimetype;
    bool image = false;
    int width = 0, height = 0;
    std::string key;  // its picture, known to the window already
  };
  std::vector<prepared_file> to_send;
  std::uint64_t files_made = 0;
  void apply(const request::attach_files&);
  void apply(const request::close_send_box&);
  // Files given: read, a picture known by its bytes; a picture dropped on
  // the window written anew from its pixels -- nothing of its file, its
  // metadata among it, goes with it -- and named image.<its type>. Then the
  // send box, with what was waiting in it before.
  void files_given(std::vector<std::string> paths, bool dropped);
  // Sent: each file, the caption with the first; the box closed.
  void apply(const request::send_files&);

  // A message swiped to the left: answered, as its menu's Reply does.
  void apply(const request::reply_to& one);
  void apply(const request::menu_reply&);
  void apply(const request::menu_edit&);
  void apply(const request::menu_copy&);
  // A reaction: the user's own put where it is not, taken back where it
  // is -- shown at once, and told to the server.
  void apply(const request::react& one);
  void apply(const request::menu_react& one);
  void apply(const request::menu_copy_link&);
  // A picture or a file saved into Downloads, from the menu.
  void apply(const request::menu_save&);
  void apply(const request::menu_delete&);
  // A link pressed: to a user, a room or a message, in here -- matrix.to,
  // matrix: and xmpp: links -- and anywhere else, in the browser.
  void apply(const request::open_url& one);

  // What a link points at, in here.
  struct link_target {
    std::string id;                    // @user, !room, #alias, or a JID
    std::optional<std::string> event;  // $event, in the room
    std::vector<std::string> via;      // the servers to join through
    bool xmpp = false;
  };
  static std::string percent_decoded(std::string_view text);
  static std::optional<link_target> link_target_of(std::string_view url);

  // A link followed: the chat it names, opened -- and the message in it,
  // jumped to -- or the person, their page; a room not joined yet, joined,
  // and opened when it comes.
  std::optional<link_target> pending_link;
  void open_chat(const mux::conversation_id& which, const std::optional<std::string>& event);
  std::optional<mux::conversation_id> chat_of(const link_target& where) const;
  void go_to(const link_target& where);
  void apply(const request::cancel_compose&);
  // Older messages of a chat: from the disk while it has some from before
  // the oldest in memory, from the server past that.
  void apply(const request::load_older& one);
  void apply(const request::submit_message& one);
  void apply(const request::resize_sidebar& one);
  // A member written to: their direct chat, where there is one already.
  void apply(const request::message_person& one);
  void apply(const request::jump_to_message& one);

  // A sender pressed in the messages: their page, in the chat's info.
  void apply(const request::open_member_info& one);
  void apply(const request::not_implemented& one);
  void apply(const request::close_notice&);
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
    std::visit(
        [&](accounts& panel) {
          if (!panel.selected)
            return;
          if (const auto found = this->find(*panel.selected); found != saved.end())
            f(panel, *found);
        },
        *up);
  }
  void apply(const request::account_page& one);
  void apply(const request::flip_account_receipts&);
  void apply(const request::flip_account_typing&);
  // The user typing in the chosen chat, or not: said, where the account's
  // privacy lets it -- 'typing' at most every twenty seconds while it goes
  // on (it lasts thirty), 'stopped' when the field is emptied or sent.
  std::optional<mux::conversation_id> typing_in;
  std::chrono::steady_clock::time_point typing_said{};
  void apply(const request::typing& one);
  void apply(const request::proxy_kind& one);
  // The chosen account through a profile, or none: kept, and connected again.
  void apply(const request::choose_account_proxy& one);
  void reconnect(const mux::config::account_t& account);
  // The accounts going through a profile, connected again.
  void reconnect_through(const std::string& name);
  void apply(const request::settings_appearance&);
  void apply(const request::settings_files&);
  void apply(const request::flip_strip_metadata&);
  void apply(const request::flip_rename_pictures&);
  void apply(const request::settings_storage&);
  // A limit halved or doubled, within its bounds: kept, and in force at once.
  void apply(const request::change_limit& one);
  // What is kept on disk, gone: the stored messages and the pictures, and
  // the pictures in memory, to be fetched again as they are wanted.
  void apply(const request::clear_stored&);
  void apply(const request::settings_rendering&);
  // The theme or the renderer chosen: kept, for the next start.
  // A theme chosen: its colours in place, and the window made again in them,
  // as it was -- the chats, the one chosen, the widths -- with Settings open
  // where it was.
  void apply(const request::set_theme& one);
  // An accent chosen: the same as a theme, over it.
  void apply(const request::set_accent& one);
  // The theme and accent now chosen put in place, and the window made again
  // in them, as it was, with Settings open on Appearance.
  void rebuild_in_theme();
  void apply(const request::set_renderer& one);
  void show_appearance_choices();
  void apply(const request::manage_proxies&);
  void apply(const request::settings_proxies&);
  void apply(const request::add_proxy&);
  void apply(const request::edit_proxy& one);
  // A profile saved: a new one added, or one changed -- and renamed in the
  // accounts that use it.
  void apply(const request::save_proxy_profile&);
  // A profile deleted: the accounts that used it connect directly.
  void apply(const request::delete_proxy_profile&);


  void apply(const request::send_typed&);
  // What is in the message field, to the chosen chat -- a new message, an
  // answer to one, or one edited -- and the field emptied.
  void send_message(std::string text);
  // Another account's chats listed: the drawer goes back, and no chat is
  // chosen.
  void apply(const request::switch_account& one);
  void apply(const request::pop_panel&);
  void apply(const request::settings_home&);
  void apply(const request::settings_animations&);

  // How much moves, from now on and in the file.
  void set_motion(std::string level);

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
    std::visit(mux::overloaded{[](mux::config::matrix_account& now, const mux::config::matrix_account& before) {
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

  // Written, unless the file there could not be read: that one is the
  // user's to look at, not to lose.
  [[nodiscard]] std::optional<std::string> write();
  void save_from_accounts();
};

}  // namespace mux::app
