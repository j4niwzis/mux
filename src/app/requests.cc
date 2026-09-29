// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.requests: What the window asks: requests, and the actions that make them.
export module mux.app.requests;

import std;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.network;

export namespace mux::app {

// What the message field's text is for.
namespace compose {
struct plain {};
struct reply {
  std::string id;
};
struct edit {
  std::string id;
};
}  // namespace compose
using compose_t = std::variant<compose::plain, compose::reply, compose::edit>;

namespace request {
struct choose {
  mux::conversation_id which;
};
struct back {};
struct open_accounts {};
struct open_new_account {};
struct add_xmpp {};
struct add_matrix {};
struct select_account {
  std::string address;
};
struct toggle_advanced {};
struct toggle_plain {};
struct submit_login {};
struct flip_enabled {
  std::string address;
};
struct remove_account {
  std::string address;
};
struct open_drawer {};
struct show_account {
  std::string address;
};
struct set_motion {
  std::string level;
};
struct quit {};
struct open_settings {};
struct pop_panel {};
struct toggle_info {};
struct jump_to_end {};
using message_menu = mux::ui::menu_facts;
struct menu_copy_link {};
struct react {
  std::string id;
  std::string key;
};
struct menu_react {
  std::string key;
};
struct menu_save {};
struct close_menu {};
struct menu_reply {};
struct menu_edit {};
struct menu_copy {};
struct menu_delete {};
struct cancel_compose {};
struct open_url {
  std::string url;
};
struct load_context {
  mux::conversation_id in;
  std::string target;
};
struct load_newer {
  mux::conversation_id in;
  std::string from;
};
struct load_older {
  mux::conversation_id in;
  std::string from;
};
struct submit_message {
  std::string text;
};
struct send_typed {};
struct resize_sidebar {
  float x = 0.0f;
};
struct message_person {
  mux::conversation_id who;
};
struct attach_files {};
struct settings_files {};
struct flip_strip_metadata {};
struct flip_show_deleted {};
struct flip_rename_pictures {};
struct close_send_box {};
struct send_files {};
struct open_picture {
  std::string source;
  std::string sender, name, when;
};
struct save_picture {
  std::string source;
};
// An avatar pressed -- a person's, by their id, or a chat's: its picture in
// the viewer, as a message's picture opens, to be looked at or saved.
struct open_avatar {
  std::string key;
};
struct close_picture {};
struct open_file {
  std::string source;
  std::string name;
};
struct reply_to {
  std::string id;
  std::string text;
};
struct jump_to_message {
  std::string id;
  // The part of it a reply quoted, to be marked in it as Telegram does.
  std::optional<std::string> fragment;
};
struct open_search {};
struct edit_last {};
struct reply_step {
  bool older = true;
};
struct close_search {};
struct search_typed {
  std::string text;
};
struct search_step {
  bool older = true;
};
struct open_member_info {
  std::string id;
};
struct not_implemented {
  std::string what;
};
struct close_notice {};
struct close_person_info {};
struct close_room_card {};
struct jump_to_mark {
  mux::mark_kind_t kind;
};
struct list_marks {
  mux::mark_kind_t kind;
};
struct go_to_mark {
  mux::mark_kind_t kind;
  std::string event;
};
struct close_marks {};
struct settings_notifications {};
struct flip_notify {
  mux::config::notify_flag_t flag;
};
struct set_notify_backend {
  mux::config::notify_backend_t backend;
};
struct flip_account_notify {};
struct flip_account_notify_sound {};
struct set_chat_notify {
  mux::config::notify_mode_t mode;
};
// Room events shown or not at a level: all of them, or one kind; none said,
// as the level under says.
struct set_room_event_kind {
  mux::choice_level_t level;
  std::optional<mux::room_event_t> kind;
  std::optional<bool> show;
};
struct join_room_card {};
struct toggle_emoji {};
struct close_emoji {};
struct insert_emoji {
  std::string text;
};
// The GIFs: one saved from its menu, the saved ones shown in the input's
// panel, and one of them sent.
struct menu_save_gif {};
struct menu_pin {};
// A message's reactions as the events they are, in a window of their own.
struct menu_reactions {};
// The room's management: opened from its info, closed, and what is done in it.
// Whether room events show: for every chat, for the chosen account's, or for
// the chat being read.
struct flip_room_events {};
struct flip_account_room_events {};
struct flip_chat_room_events {};
// Forwarding: the box of chats to forward to, closed, and a chat chosen.
// A new chat: its box opened and closed; a direct chat with someone, or a
// group, asked for.
struct open_new_chat {};
struct close_new_chat {};
struct start_direct {
  std::string user;
};
struct start_group {
  std::string name;
};
// The developer tools: an event's source, the room's state, an event sent.
struct menu_view_source {};
struct explore_state {};
struct open_send_custom {};
struct close_devtools {};
struct send_custom {
  std::string type;
  std::optional<std::string> state_key;
  std::string json;
};
struct menu_forward {};
struct close_forward {};
struct forward_to {
  mux::conversation_id to;
};
struct open_manage {};
struct close_manage {};
struct room_act {
  mux::room_action_t action;
};
struct close_reactions {};
struct show_gifs {};
struct send_gif {
  std::string path;
};
struct send_sticker {
  mux::emote sticker;
};
// A voice message or an audio file pressed: played, paused, played on.
struct play_audio {
  std::string source;
};
struct choose_new_proxy {
  int index = -1;
};
struct resize_info {
  float x = 0.0f;
};
struct toggle_mute {};
struct close_account_pages {};
struct accounts_back {};
struct account_page {
  int page = 0;
};
struct flip_account_receipts {};
struct flip_account_typing {};
struct typing {
  bool on = false;
};
struct proxy_kind {
  mux::config::proxy_kind_t kind;
};
struct settings_rendering {};
struct settings_storage {};
struct change_limit {
  mux::config::limit_t which;
  bool more = true;
};
struct clear_stored {};
struct choose_account_proxy {
  int index = -1;
};
struct manage_proxies {};
struct settings_proxies {};
struct add_proxy {};
struct edit_proxy {
  int index = 0;
};
struct save_proxy_profile {};
struct delete_proxy_profile {};
struct settings_appearance {};
struct set_theme {
  mux::config::theme_t theme;
};
struct set_renderer {
  mux::config::renderer_t renderer;
};
struct set_accent {
  mux::config::accent_t accent;
};
struct leave_chat {};
struct switch_account {
  std::string address;
};
struct close_settings {};
struct settings_home {};
struct settings_animations {};
}  // namespace request

using request_t =
    std::variant<request::choose, request::back, request::open_accounts, request::open_new_account,
                 request::add_xmpp, request::add_matrix, request::select_account, request::toggle_advanced,
                 request::toggle_plain, request::submit_login, request::flip_enabled, request::remove_account,
                 request::open_drawer, request::show_account, request::set_motion, request::quit,
                 request::open_settings, request::close_settings, request::settings_home,
                 request::settings_animations, request::pop_panel, request::toggle_info, request::load_older, request::load_context, request::load_newer, request::jump_to_end, request::message_menu, request::menu_copy_link, request::menu_save, request::react, request::menu_react,
                 request::close_menu, request::menu_reply, request::menu_edit, request::menu_copy,
                 request::menu_delete, request::cancel_compose, request::open_url,
                 request::switch_account, request::submit_message, request::send_typed,
                 request::resize_sidebar, request::not_implemented, request::message_person, request::jump_to_message, request::open_search, request::edit_last, request::reply_step, request::close_search, request::search_typed, request::search_step, request::open_member_info, request::reply_to, request::open_picture, request::open_avatar, request::close_picture, request::save_picture, request::open_file, request::attach_files, request::close_send_box, request::send_files, request::settings_files, request::flip_strip_metadata, request::flip_show_deleted, request::flip_rename_pictures, request::close_notice, request::close_person_info, request::close_room_card, request::join_room_card, request::jump_to_mark, request::list_marks, request::go_to_mark, request::close_marks, request::settings_notifications, request::flip_notify, request::set_notify_backend, request::flip_account_notify, request::flip_account_notify_sound, request::set_chat_notify, request::set_room_event_kind, request::toggle_emoji, request::close_emoji, request::insert_emoji, request::menu_save_gif, request::menu_pin, request::menu_reactions, request::close_reactions, request::open_manage, request::close_manage, request::room_act, request::menu_forward, request::close_forward, request::forward_to, request::menu_view_source, request::explore_state, request::open_send_custom, request::close_devtools, request::send_custom, request::open_new_chat, request::close_new_chat, request::start_direct, request::start_group, request::flip_room_events, request::flip_account_room_events, request::flip_chat_room_events, request::show_gifs, request::send_gif, request::send_sticker, request::play_audio,
                 request::resize_info, request::choose_new_proxy, request::toggle_mute, request::close_account_pages,
                 request::accounts_back, request::account_page, request::flip_account_receipts, request::flip_account_typing, request::typing,
                 request::proxy_kind, request::choose_account_proxy, request::manage_proxies,
                 request::settings_proxies, request::add_proxy, request::edit_proxy, request::save_proxy_profile,
                 request::delete_proxy_profile, request::settings_appearance, request::settings_rendering, request::settings_storage, request::change_limit, request::clear_stored, request::set_theme,
                 request::set_renderer, request::set_accent, request::leave_chat>;

// What the screens ask: each a request, kept until the program applies it
// between events -- except a message, which goes to the network at once.
struct actions {
  network* net = nullptr;
  // In the demo, a message sent is there at once, as sent.
  bool demo = false;
  mailbox_type* box = nullptr;
  int demo_sent = 0;
  std::vector<request_t> requests;

  void choose(const mux::conversation_id& which) { requests.emplace_back(request::choose{which}); }
  void send(const mux::conversation_id& to, std::string text) {
    if (!demo) {
      net->send(to, std::move(text));
      return;
    }
    mux::message one;
    one.in = to;
    one.id = std::format("demo-sent-{}", demo_sent++);
    one.sender = to.account.address;
    one.at = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    one.body.plain = std::move(text);
    one.outgoing = true;
    box->push(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  }
  void back() { requests.emplace_back(request::back{}); }
  void open_accounts() { requests.emplace_back(request::open_accounts{}); }
  void open_new_account() { requests.emplace_back(request::open_new_account{}); }
  void add_xmpp() { requests.emplace_back(request::add_xmpp{}); }
  void add_matrix() { requests.emplace_back(request::add_matrix{}); }
  void select_account(std::string address) { requests.emplace_back(request::select_account{std::move(address)}); }
  void toggle_advanced() { requests.emplace_back(request::toggle_advanced{}); }
  void toggle_plain() { requests.emplace_back(request::toggle_plain{}); }
  void submit_login() { requests.emplace_back(request::submit_login{}); }
  void flip_enabled(std::string address) { requests.emplace_back(request::flip_enabled{std::move(address)}); }
  void remove_account(std::string address) { requests.emplace_back(request::remove_account{std::move(address)}); }
  void open_drawer() { requests.emplace_back(request::open_drawer{}); }
  void show_account(std::string address) { requests.emplace_back(request::show_account{std::move(address)}); }
  void set_motion(std::string level) { requests.emplace_back(request::set_motion{std::move(level)}); }
  void quit() { requests.emplace_back(request::quit{}); }
  void open_settings() { requests.emplace_back(request::open_settings{}); }
  void pop_panel() { requests.emplace_back(request::pop_panel{}); }
  void toggle_info() { requests.emplace_back(request::toggle_info{}); }
  void jump_to_end() { requests.emplace_back(request::jump_to_end{}); }
  void message_menu(mux::ui::menu_facts facts) { requests.emplace_back(std::move(facts)); }
  void menu_copy_link() { requests.emplace_back(request::menu_copy_link{}); }
  void react(std::string id, std::string key) { requests.emplace_back(request::react{std::move(id), std::move(key)}); }
  void menu_react(std::string key) { requests.emplace_back(request::menu_react{std::move(key)}); }
  void menu_save() { requests.emplace_back(request::menu_save{}); }
  void close_menu() { requests.emplace_back(request::close_menu{}); }
  void menu_reply() { requests.emplace_back(request::menu_reply{}); }
  void menu_edit() { requests.emplace_back(request::menu_edit{}); }
  void menu_copy() { requests.emplace_back(request::menu_copy{}); }
  void menu_delete() { requests.emplace_back(request::menu_delete{}); }
  void cancel_compose() { requests.emplace_back(request::cancel_compose{}); }
  void open_url(std::string url) { requests.emplace_back(request::open_url{std::move(url)}); }
  void load_context(const mux::conversation_id& in, std::string target) {
    requests.emplace_back(request::load_context{in, std::move(target)});
  }
  void load_newer(const mux::conversation_id& in, std::string from) {
    requests.emplace_back(request::load_newer{in, std::move(from)});
  }
  void load_older(const mux::conversation_id& in, std::string from) {
    requests.emplace_back(request::load_older{in, std::move(from)});
  }
  void submit_message(std::string text) { requests.emplace_back(request::submit_message{std::move(text)}); }
  void send_typed() { requests.emplace_back(request::send_typed{}); }
  void resize_sidebar(float x) { requests.emplace_back(request::resize_sidebar{x}); }
  void message_person(const mux::conversation_id& who) { requests.emplace_back(request::message_person{who}); }
  void attach_files() { requests.emplace_back(request::attach_files{}); }
  void settings_files() { requests.emplace_back(request::settings_files{}); }
  void flip_strip_metadata() { requests.emplace_back(request::flip_strip_metadata{}); }
  void flip_show_deleted() { requests.emplace_back(request::flip_show_deleted{}); }
  void flip_rename_pictures() { requests.emplace_back(request::flip_rename_pictures{}); }
  void close_send_box() { requests.emplace_back(request::close_send_box{}); }
  void send_files() { requests.emplace_back(request::send_files{}); }
  void open_avatar(std::string key) { requests.emplace_back(request::open_avatar{std::move(key)}); }
  void open_picture(std::string source, std::string sender, std::string name, std::string when) {
    requests.emplace_back(request::open_picture{std::move(source), std::move(sender), std::move(name), std::move(when)});
  }
  void save_picture(std::string source) { requests.emplace_back(request::save_picture{std::move(source)}); }
  void close_picture() { requests.emplace_back(request::close_picture{}); }
  void open_file(std::string source, std::string name) {
    requests.emplace_back(request::open_file{std::move(source), std::move(name)});
  }
  void reply_to(std::string id, std::string text) { requests.emplace_back(request::reply_to{std::move(id), std::move(text)}); }
  void jump_to_message(std::string id, std::optional<std::string> fragment = std::nullopt) {
    requests.emplace_back(request::jump_to_message{std::move(id), std::move(fragment)});
  }
  void open_search() { requests.emplace_back(request::open_search{}); }
  void close_search() { requests.emplace_back(request::close_search{}); }
  void search_typed(std::string text) { requests.emplace_back(request::search_typed{std::move(text)}); }
  void search_step(bool older) { requests.emplace_back(request::search_step{older}); }
  void edit_last() { requests.emplace_back(request::edit_last{}); }
  void reply_step(bool older) { requests.emplace_back(request::reply_step{older}); }
  void open_member_info(std::string id) { requests.emplace_back(request::open_member_info{std::move(id)}); }
  void not_implemented(std::string what) { requests.emplace_back(request::not_implemented{std::move(what)}); }
  void close_notice() { requests.emplace_back(request::close_notice{}); }
  void close_person_info() { requests.emplace_back(request::close_person_info{}); }
  void close_room_card() { requests.emplace_back(request::close_room_card{}); }
  void jump_to_mark(mux::mark_kind_t kind) { requests.emplace_back(request::jump_to_mark{kind}); }
  void list_marks(mux::mark_kind_t kind) { requests.emplace_back(request::list_marks{kind}); }
  void go_to_mark(mux::mark_kind_t kind, std::string event) {
    requests.emplace_back(request::go_to_mark{kind, std::move(event)});
  }
  void close_marks() { requests.emplace_back(request::close_marks{}); }
  void settings_notifications() { requests.emplace_back(request::settings_notifications{}); }
  void flip_notify(mux::config::notify_flag_t flag) { requests.emplace_back(request::flip_notify{flag}); }
  void set_notify_backend(mux::config::notify_backend_t backend) {
    requests.emplace_back(request::set_notify_backend{backend});
  }
  void flip_account_notify() { requests.emplace_back(request::flip_account_notify{}); }
  void flip_account_notify_sound() { requests.emplace_back(request::flip_account_notify_sound{}); }
  void set_chat_notify(mux::config::notify_mode_t mode) { requests.emplace_back(request::set_chat_notify{mode}); }
  void set_room_event_kind(mux::choice_level_t level, std::optional<mux::room_event_t> kind, std::optional<bool> show) {
    requests.emplace_back(request::set_room_event_kind{level, kind, show});
  }
  void join_room_card() { requests.emplace_back(request::join_room_card{}); }
  void toggle_emoji() { requests.emplace_back(request::toggle_emoji{}); }
  void close_emoji() { requests.emplace_back(request::close_emoji{}); }
  void menu_save_gif() { requests.emplace_back(request::menu_save_gif{}); }
  void menu_pin() { requests.emplace_back(request::menu_pin{}); }
  void menu_reactions() { requests.emplace_back(request::menu_reactions{}); }
  void open_manage() { requests.emplace_back(request::open_manage{}); }
  void menu_forward() { requests.emplace_back(request::menu_forward{}); }
  void menu_view_source() { requests.emplace_back(request::menu_view_source{}); }
  void explore_state() { requests.emplace_back(request::explore_state{}); }
  void open_send_custom() { requests.emplace_back(request::open_send_custom{}); }
  void close_devtools() { requests.emplace_back(request::close_devtools{}); }
  void send_custom(std::string type, std::optional<std::string> key, std::string json) {
    requests.emplace_back(request::send_custom{std::move(type), std::move(key), std::move(json)});
  }
  void open_new_chat() { requests.emplace_back(request::open_new_chat{}); }
  void close_new_chat() { requests.emplace_back(request::close_new_chat{}); }
  void start_direct(std::string user) { requests.emplace_back(request::start_direct{std::move(user)}); }
  void start_group(std::string name) { requests.emplace_back(request::start_group{std::move(name)}); }
  void close_forward() { requests.emplace_back(request::close_forward{}); }
  void forward_to(mux::conversation_id to) { requests.emplace_back(request::forward_to{std::move(to)}); }
  void flip_room_events() { requests.emplace_back(request::flip_room_events{}); }
  void flip_account_room_events() { requests.emplace_back(request::flip_account_room_events{}); }
  void flip_chat_room_events() { requests.emplace_back(request::flip_chat_room_events{}); }
  void close_manage() { requests.emplace_back(request::close_manage{}); }
  void room_act(mux::room_action_t action) { requests.emplace_back(request::room_act{std::move(action)}); }
  void close_reactions() { requests.emplace_back(request::close_reactions{}); }
  void show_gifs() { requests.emplace_back(request::show_gifs{}); }
  void send_gif(std::string path) { requests.emplace_back(request::send_gif{std::move(path)}); }
  void send_sticker(mux::emote sticker) { requests.emplace_back(request::send_sticker{std::move(sticker)}); }
  void play_audio(std::string source) { requests.emplace_back(request::play_audio{std::move(source)}); }
  void insert_emoji(std::string text) { requests.emplace_back(request::insert_emoji{std::move(text)}); }
  void resize_info(float x) { requests.emplace_back(request::resize_info{x}); }
  void choose_new_proxy(int index) { requests.emplace_back(request::choose_new_proxy{index}); }
  void toggle_mute() { requests.emplace_back(request::toggle_mute{}); }
  void close_account_pages() { requests.emplace_back(request::close_account_pages{}); }
  void accounts_back() { requests.emplace_back(request::accounts_back{}); }
  void account_page(int page) { requests.emplace_back(request::account_page{page}); }
  void flip_account_receipts() { requests.emplace_back(request::flip_account_receipts{}); }
  void flip_account_typing() { requests.emplace_back(request::flip_account_typing{}); }
  void typing(bool on) { requests.emplace_back(request::typing{on}); }
  void proxy_kind(mux::config::proxy_kind_t kind) { requests.emplace_back(request::proxy_kind{kind}); }
  void settings_rendering() { requests.emplace_back(request::settings_rendering{}); }
  void settings_storage() { requests.emplace_back(request::settings_storage{}); }
  void change_limit(mux::config::limit_t which, bool more) { requests.emplace_back(request::change_limit{which, more}); }
  void clear_stored() { requests.emplace_back(request::clear_stored{}); }
  void choose_account_proxy(int index) { requests.emplace_back(request::choose_account_proxy{index}); }
  void manage_proxies() { requests.emplace_back(request::manage_proxies{}); }
  void settings_proxies() { requests.emplace_back(request::settings_proxies{}); }
  void add_proxy() { requests.emplace_back(request::add_proxy{}); }
  void edit_proxy(int index) { requests.emplace_back(request::edit_proxy{index}); }
  void save_proxy_profile() { requests.emplace_back(request::save_proxy_profile{}); }
  void delete_proxy_profile() { requests.emplace_back(request::delete_proxy_profile{}); }
  void settings_appearance() { requests.emplace_back(request::settings_appearance{}); }
  void set_theme(mux::config::theme_t theme) { requests.emplace_back(request::set_theme{theme}); }
  void set_renderer(mux::config::renderer_t renderer) { requests.emplace_back(request::set_renderer{renderer}); }
  void set_accent(mux::config::accent_t accent) { requests.emplace_back(request::set_accent{accent}); }
  void leave_chat() { requests.emplace_back(request::leave_chat{}); }
  void switch_account(std::string address) { requests.emplace_back(request::switch_account{std::move(address)}); }
  void close_settings() { requests.emplace_back(request::close_settings{}); }
  void settings_home() { requests.emplace_back(request::settings_home{}); }
  void settings_animations() { requests.emplace_back(request::settings_animations{}); }
};

using window_type = mux::ui::window<actions>;

}  // namespace mux::app

// The window's scene is instantiated once, in src/app/scene.cc: its walks
// over the whole tree -- routing, layout, drawing, for every type of node
// in it -- were most of a build, made again in every unit that touched the
// scene. In a named module, members defined in a class are not implicitly
// inline, so this keeps them all out of the other units.
extern template class skiff::scene::Scene<mux::app::window_type>;
