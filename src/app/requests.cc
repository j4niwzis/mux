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
// The viewer's loader pressed: its download stopped, or started again.
struct press_loader {
  std::string source;
};
// The search for a message jumped to stopped: its loader's cross.
struct stop_jump {};
// A video pressed: the viewer on its thumbnail, and it fetched and played.
struct open_video {
  std::string source;  // its thumbnail
  std::string video;
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
struct open_explore {};
struct close_explore {};
struct search_rooms {
  std::string server;
  std::string query;
};
struct join_directory_room {
  std::string room;
  std::string server;
};
struct create_room {
  std::string name;
  std::string topic;
  bool open = false;
  std::string alias;
};
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
  std::string picture;  // a custom emoji's, where it is one
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
// Who has read up to where, as faces: shown or not at a level, or (at an
// account's or a chat's) as the level over it says.
struct set_receipts_shown {
  mux::choice_level_t level;
  std::optional<bool> show;
};
// How far a jump's search pages back, at a level: a number of events, 0 for
// no limit, or (at an account's or a chat's) as the level over it says.
struct set_jump_search {
  mux::choice_level_t level;
  std::optional<std::int64_t> most;
};
// Link previews, at a level: on, off, or (at an account's or a chat's) as
// the level over it says.
struct set_link_previews {
  mux::choice_level_t level;
  std::optional<bool> show;
};
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
struct flip_partial_redraw {};
struct flip_flash_redraws {};
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

// The requests, in groups of fifteen, and a request one of the groups: one
// variant of all of them made its every alternative's constructor through a
// recursive union as deep as its place -- quadratic in how many there are,
// and most of this unit's build. Grouped, a request is found in its group
// by the outer variant's converting constructor, and applied by two visits.
namespace request {
using group_0 = std::variant<choose, back, open_accounts, open_new_account, add_xmpp, add_matrix, select_account, toggle_advanced, toggle_plain, submit_login, flip_enabled, remove_account, open_drawer, show_account, set_motion>;
using group_1 = std::variant<quit, open_settings, close_settings, settings_home, settings_animations, pop_panel, toggle_info, load_older, load_context, load_newer, jump_to_end, message_menu, menu_copy_link, menu_save, react>;
using group_2 = std::variant<menu_react, close_menu, menu_reply, menu_edit, menu_copy, menu_delete, cancel_compose, open_url, switch_account, submit_message, send_typed, resize_sidebar, not_implemented, message_person, jump_to_message>;
using group_3 = std::variant<open_search, edit_last, reply_step, close_search, search_typed, search_step, open_member_info, reply_to, open_picture, open_avatar, close_picture, save_picture, open_video, stop_jump, press_loader>;
using group_4 = std::variant<open_file, attach_files, close_send_box, send_files, settings_files, flip_strip_metadata, flip_show_deleted, flip_rename_pictures, close_notice, close_person_info, close_room_card, join_room_card, jump_to_mark, list_marks, go_to_mark>;
using group_5 = std::variant<close_marks, open_explore, close_explore, search_rooms, join_directory_room, create_room, settings_notifications, flip_notify, set_notify_backend, flip_account_notify, flip_account_notify_sound, set_chat_notify, set_room_event_kind, set_receipts_shown, set_link_previews>;
using group_6 = std::variant<set_jump_search, toggle_emoji, close_emoji, insert_emoji, menu_save_gif, menu_pin, menu_reactions, close_reactions, open_manage, close_manage, room_act, menu_forward, close_forward, forward_to, menu_view_source>;
using group_7 = std::variant<explore_state, open_send_custom, close_devtools, send_custom, open_new_chat, close_new_chat, start_direct, start_group, flip_room_events, flip_account_room_events, flip_chat_room_events, show_gifs, send_gif, send_sticker, play_audio>;
using group_8 = std::variant<resize_info, choose_new_proxy, toggle_mute, close_account_pages, accounts_back, account_page, flip_account_receipts, flip_account_typing, typing, proxy_kind, choose_account_proxy, manage_proxies, settings_proxies, add_proxy, edit_proxy>;
using group_9 = std::variant<save_proxy_profile, delete_proxy_profile, settings_appearance, settings_rendering, settings_storage, change_limit, clear_stored, set_theme, set_renderer, flip_partial_redraw, flip_flash_redraws, set_accent, leave_chat>;
}  // namespace request
using request_t = std::variant<request::group_0, request::group_1, request::group_2, request::group_3, request::group_4, request::group_5, request::group_6, request::group_7, request::group_8, request::group_9>;

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
  void stop_jump() { requests.emplace_back(request::stop_jump{}); }
  void open_video(std::string source, std::string video, std::string sender, std::string name, std::string when) {
    requests.emplace_back(request::open_video{std::move(source), std::move(video), std::move(sender), std::move(name),
                                              std::move(when)});
  }
  void save_picture(std::string source) { requests.emplace_back(request::save_picture{std::move(source)}); }
  void press_loader(std::string source) { requests.emplace_back(request::press_loader{std::move(source)}); }
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
  void open_explore() { requests.emplace_back(request::open_explore{}); }
  void close_explore() { requests.emplace_back(request::close_explore{}); }
  void search_rooms(std::string server, std::string query) {
    requests.emplace_back(request::search_rooms{std::move(server), std::move(query)});
  }
  void join_directory_room(std::string room, std::string server) {
    requests.emplace_back(request::join_directory_room{std::move(room), std::move(server)});
  }
  void create_room(std::string name, std::string topic, bool open, std::string alias) {
    requests.emplace_back(request::create_room{std::move(name), std::move(topic), open, std::move(alias)});
  }
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
  void set_jump_search(mux::choice_level_t level, std::optional<std::int64_t> most) {
    requests.emplace_back(request::set_jump_search{level, most});
  }
  void set_link_previews(mux::choice_level_t level, std::optional<bool> show) {
    requests.emplace_back(request::set_link_previews{level, show});
  }
  void set_receipts_shown(mux::choice_level_t level, std::optional<bool> show) {
    requests.emplace_back(request::set_receipts_shown{level, show});
  }
  void flip_account_room_events() { requests.emplace_back(request::flip_account_room_events{}); }
  void flip_chat_room_events() { requests.emplace_back(request::flip_chat_room_events{}); }
  void close_manage() { requests.emplace_back(request::close_manage{}); }
  void room_act(mux::room_action_t action) { requests.emplace_back(request::room_act{std::move(action)}); }
  void close_reactions() { requests.emplace_back(request::close_reactions{}); }
  void show_gifs() { requests.emplace_back(request::show_gifs{}); }
  void send_gif(std::string path) { requests.emplace_back(request::send_gif{std::move(path)}); }
  void send_sticker(mux::emote sticker) { requests.emplace_back(request::send_sticker{std::move(sticker)}); }
  void play_audio(std::string source) { requests.emplace_back(request::play_audio{std::move(source)}); }
  void insert_emoji(std::string text, std::string picture = {}) {
    requests.emplace_back(request::insert_emoji{std::move(text), std::move(picture)});
  }
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
  void flip_partial_redraw() { requests.emplace_back(request::flip_partial_redraw{}); }
  void flip_flash_redraws() { requests.emplace_back(request::flip_flash_redraws{}); }
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
// Outside a release build, where each child is walked through its table:
// the tables of the window's big subtrees -- and so the walks of all in
// them -- made in units of their own (walks_*.cc), in parallel, not where
// the window is walked. A release build walks statically and uses none.
// And the window's layers -- the frame, every dialog's shell, the popups --
// so that the scene's own unit walks the window alone.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::window<mux::app::actions>::layers> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::window<mux::app::actions>::layers>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::drawer_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::drawer_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::accounts_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::accounts_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::settings_dialog<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::settings_dialog<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::room_settings<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_settings<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::explore_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::explore_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::new_chat_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::new_chat_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::person_card<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::person_card<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::room_card<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::room_card<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::reactions_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::reactions_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::marks_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::marks_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::forward_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::forward_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::devtools_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::devtools_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::send_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::send_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::notice_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::notice_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::emoji_popup<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::emoji_popup<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::context_menu<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::context_menu<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::picture_viewer<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::picture_viewer<mux::app::actions>>() noexcept;
