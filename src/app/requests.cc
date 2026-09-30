// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.requests: What the window asks: requests, and the actions that make them.
export module mux.app.requests;

import std;
import splice;
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
  // What the field held before the edit began: put back when it is let go.
  std::string before;
};
}  // namespace compose
using compose_t = splice::variant<compose::plain, compose::reply, compose::edit>;

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
// A picture copied to the clipboard: the one a message's menu is up for,
// or one by its source (the viewer's).
struct menu_copy_image {};
struct copy_picture {
  std::string source;
};
struct return_to_chat {};
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
struct menu_quote_reply {};
struct menu_edit {};
struct menu_copy {};
struct menu_delete {};
struct cancel_compose {};
// Element's unsent bar: the messages here the server did not take, sent
// again, or let go.
struct retry_unsent {};
struct discard_unsent {};
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
  // The message pressed to go there -- a reply, a reaction's line: where
  // "↓" comes back to.
  std::optional<std::string> from;
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
// A space's rooms and spaces, joined or not, in Explore.
struct explore_space {
  std::string room;
  std::string name;  // as listed, where it is not joined: none, the chat's
};
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
  bool federate = true;  // those of other servers may join
};
// Element's Start chat and Create a room: people searched for, a room's
// box opened and closed; and a text put on the clipboard (one's link).
struct find_people {
  std::string query;
};
struct open_new_room {};
// Emojis & Stickers: one's own pack, or the room's packs; a pack saved or
// taken away; images chosen for the pack open.
// Threads: their panel opened or closed, one opened or let go (back to the
// list), an answer sent in one, one begun from a message's menu.
// A chat background: its dialog, for a level; what is chosen there.
struct open_wallpaper {
  mux::choice_level_t level;
};
struct close_wallpaper {};
// The window's opacity, from the next start; the background behind all of it.
struct set_window_opacity {
  int percent = 100;
};
struct flip_wallpaper_behind {};
// Spaces: a bar's order, as a drag left it -- the item moved out of the
// bar it came from, where it came from one; an item's bars, as chosen;
// the bars at all, and the top one.
struct place_spaces {
  std::string account;
  mux::config::space_bar_t bar;
  std::vector<mux::config::space_item_t> order;
  std::optional<mux::config::space_bar_t> from;
  std::optional<mux::config::space_item_t> moved;
};
struct set_space_bars {
  std::string account;
  mux::config::space_item_t item;
  bool side = true;
  bool top = false;
};
struct flip_spaces {};
struct flip_top_bar {};
// How much Frosted blurs, in percent.
struct set_frost_blur {
  int percent = 30;
};
// Bubbles at a level: a look, or none -- as the level over it says.
struct set_bubbles {
  mux::choice_level_t level;
  std::optional<mux::config::bubble_look> look;
  mux::config::look_part_t part = mux::config::look_part::bubbles{};
};
struct set_wallpaper {
  mux::choice_level_t level;
  mux::config::wallpaper_pick_t pick;
};
struct toggle_threads {};
struct open_thread {
  std::string root;
};
struct close_thread {};
struct send_in_thread {
  std::string root;
  std::string text;
};
struct menu_thread {};
struct open_packs {};
struct open_room_packs {};
struct close_packs {};
struct save_pack {
  mux::emote_pack pack;
};
struct delete_pack {
  std::string room;
  std::string state_key;
};
struct pick_pack_images {};
struct close_new_room {};
struct copy_text {
  std::string text;
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
// How a level shows room events, as a whole: for all of them (none: as
// above), and each kind (none: none chosen).
struct set_room_events {
  mux::choice_level_t level;
  std::optional<bool> all;
  std::optional<mux::config::room_event_kinds> kinds;
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
struct flip_vsync {};
struct flip_show_fps {};
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

// Every request, one of them: a splice::variant, built in time linear in how
// many there are (std::variant's nested union made it quadratic).
using request_t = splice::variant<request::choose, request::back, request::open_accounts, request::open_new_account, request::add_xmpp, request::add_matrix, request::select_account, request::toggle_advanced, request::toggle_plain, request::submit_login, request::flip_enabled, request::remove_account, request::open_drawer, request::show_account, request::set_motion, request::quit, request::open_settings, request::close_settings, request::settings_home, request::settings_animations, request::pop_panel, request::toggle_info, request::load_older, request::load_context, request::load_newer, request::jump_to_end, request::return_to_chat, request::menu_copy_image, request::copy_picture, request::message_menu, request::menu_copy_link, request::menu_save, request::react, request::menu_react, request::close_menu, request::menu_reply, request::menu_quote_reply, request::menu_edit, request::menu_copy, request::menu_delete, request::cancel_compose, request::retry_unsent, request::discard_unsent, request::open_url, request::switch_account, request::submit_message, request::send_typed, request::resize_sidebar, request::not_implemented, request::message_person, request::jump_to_message, request::open_search, request::edit_last, request::reply_step, request::close_search, request::search_typed, request::search_step, request::open_member_info, request::reply_to, request::open_picture, request::open_avatar, request::close_picture, request::save_picture, request::open_video, request::stop_jump, request::press_loader, request::open_file, request::attach_files, request::close_send_box, request::send_files, request::settings_files, request::flip_strip_metadata, request::flip_show_deleted, request::flip_rename_pictures, request::close_notice, request::close_person_info, request::close_room_card, request::join_room_card, request::jump_to_mark, request::list_marks, request::go_to_mark, request::close_marks, request::open_explore, request::close_explore, request::search_rooms, request::explore_space, request::join_directory_room, request::create_room, request::settings_notifications, request::flip_notify, request::set_notify_backend, request::flip_account_notify, request::flip_account_notify_sound, request::set_chat_notify, request::set_room_event_kind, request::set_room_events, request::set_receipts_shown, request::set_link_previews, request::set_jump_search, request::toggle_emoji, request::close_emoji, request::insert_emoji, request::menu_save_gif, request::menu_pin, request::menu_reactions, request::close_reactions, request::open_manage, request::close_manage, request::room_act, request::menu_forward, request::close_forward, request::forward_to, request::menu_view_source, request::explore_state, request::open_send_custom, request::close_devtools, request::send_custom, request::open_new_chat, request::close_new_chat, request::find_people, request::open_new_room, request::close_new_room, request::open_wallpaper, request::close_wallpaper, request::set_wallpaper, request::set_bubbles, request::toggle_threads, request::open_thread, request::close_thread, request::send_in_thread, request::menu_thread, request::open_packs, request::open_room_packs, request::close_packs, request::save_pack, request::delete_pack, request::pick_pack_images, request::copy_text, request::start_direct, request::start_group, request::flip_room_events, request::flip_account_room_events, request::flip_chat_room_events, request::show_gifs, request::send_gif, request::send_sticker, request::play_audio, request::resize_info, request::choose_new_proxy, request::toggle_mute, request::close_account_pages, request::accounts_back, request::account_page, request::flip_account_receipts, request::flip_account_typing, request::typing, request::proxy_kind, request::choose_account_proxy, request::manage_proxies, request::settings_proxies, request::add_proxy, request::edit_proxy, request::save_proxy_profile, request::delete_proxy_profile, request::settings_appearance, request::settings_rendering, request::settings_storage, request::change_limit, request::clear_stored, request::set_theme, request::set_renderer, request::flip_partial_redraw, request::flip_flash_redraws, request::flip_vsync, request::flip_show_fps, request::set_window_opacity, request::flip_wallpaper_behind, request::set_frost_blur, request::place_spaces, request::set_space_bars, request::flip_spaces, request::flip_top_bar, request::set_accent, request::leave_chat>;

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
  void menu_copy_image() { requests.emplace_back(request::menu_copy_image{}); }
  void copy_picture(std::string source) { requests.emplace_back(request::copy_picture{std::move(source)}); }
  void return_to_chat() { requests.emplace_back(request::return_to_chat{}); }
  void message_menu(mux::ui::menu_facts facts) { requests.emplace_back(std::move(facts)); }
  void menu_copy_link() { requests.emplace_back(request::menu_copy_link{}); }
  void react(std::string id, std::string key) { requests.emplace_back(request::react{std::move(id), std::move(key)}); }
  void menu_react(std::string key) { requests.emplace_back(request::menu_react{std::move(key)}); }
  void menu_save() { requests.emplace_back(request::menu_save{}); }
  void close_menu() { requests.emplace_back(request::close_menu{}); }
  void menu_reply() { requests.emplace_back(request::menu_reply{}); }
  void menu_quote_reply() { requests.emplace_back(request::menu_quote_reply{}); }
  void menu_edit() { requests.emplace_back(request::menu_edit{}); }
  void menu_copy() { requests.emplace_back(request::menu_copy{}); }
  void menu_delete() { requests.emplace_back(request::menu_delete{}); }
  void cancel_compose() { requests.emplace_back(request::cancel_compose{}); }
  void retry_unsent() { requests.emplace_back(request::retry_unsent{}); }
  void discard_unsent() { requests.emplace_back(request::discard_unsent{}); }
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
  void jump_to_message(std::string id, std::optional<std::string> fragment = std::nullopt,
                       std::optional<std::string> from = std::nullopt) {
    requests.emplace_back(request::jump_to_message{std::move(id), std::move(fragment), std::move(from)});
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
  void explore_space(std::string room, std::string name = {}) {
    requests.emplace_back(request::explore_space{std::move(room), std::move(name)});
  }
  void search_rooms(std::string server, std::string query) {
    requests.emplace_back(request::search_rooms{std::move(server), std::move(query)});
  }
  void join_directory_room(std::string room, std::string server) {
    requests.emplace_back(request::join_directory_room{std::move(room), std::move(server)});
  }
  void create_room(std::string name, std::string topic, bool open, std::string alias, bool federate = true) {
    requests.emplace_back(request::create_room{std::move(name), std::move(topic), open, std::move(alias), federate});
  }
  void find_people(std::string query) { requests.emplace_back(request::find_people{std::move(query)}); }
  void open_new_room() { requests.emplace_back(request::open_new_room{}); }
  void open_packs() { requests.emplace_back(request::open_packs{}); }
  void toggle_threads() { requests.emplace_back(request::toggle_threads{}); }
  void open_wallpaper(mux::choice_level_t level) { requests.emplace_back(request::open_wallpaper{level}); }
  void close_wallpaper() { requests.emplace_back(request::close_wallpaper{}); }
  void set_bubbles(mux::choice_level_t level, std::optional<mux::config::bubble_look> look,
                   mux::config::look_part_t part = mux::config::look_part::bubbles{}) {
    requests.emplace_back(request::set_bubbles{level, look, part});
  }
  void set_wallpaper(mux::choice_level_t level, mux::config::wallpaper_pick_t pick) {
    requests.emplace_back(request::set_wallpaper{level, pick});
  }
  void open_thread(std::string root) { requests.emplace_back(request::open_thread{std::move(root)}); }
  void close_thread() { requests.emplace_back(request::close_thread{}); }
  void send_in_thread(std::string root, std::string text) {
    requests.emplace_back(request::send_in_thread{std::move(root), std::move(text)});
  }
  void menu_thread() { requests.emplace_back(request::menu_thread{}); }
  void open_room_packs() { requests.emplace_back(request::open_room_packs{}); }
  void close_packs() { requests.emplace_back(request::close_packs{}); }
  void save_pack(mux::emote_pack pack) { requests.emplace_back(request::save_pack{std::move(pack)}); }
  void delete_pack(std::string room, std::string state_key) {
    requests.emplace_back(request::delete_pack{std::move(room), std::move(state_key)});
  }
  void pick_pack_images() { requests.emplace_back(request::pick_pack_images{}); }
  void close_new_room() { requests.emplace_back(request::close_new_room{}); }
  void copy_text(std::string text) { requests.emplace_back(request::copy_text{std::move(text)}); }
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
  void set_room_events(mux::choice_level_t level, std::optional<bool> all, std::optional<mux::config::room_event_kinds> kinds) {
    requests.emplace_back(request::set_room_events{level, all, std::move(kinds)});
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
  void flip_vsync() { requests.emplace_back(request::flip_vsync{}); }
  void flip_show_fps() { requests.emplace_back(request::flip_show_fps{}); }
  void set_window_opacity(int percent) { requests.emplace_back(request::set_window_opacity{percent}); }
  void flip_wallpaper_behind() { requests.emplace_back(request::flip_wallpaper_behind{}); }
  void set_frost_blur(int percent) { requests.emplace_back(request::set_frost_blur{percent}); }
  void place_spaces(std::string account, mux::config::space_bar_t bar, std::vector<mux::config::space_item_t> order,
                    std::optional<mux::config::space_bar_t> from, std::optional<mux::config::space_item_t> moved) {
    requests.emplace_back(request::place_spaces{std::move(account), bar, std::move(order), from, std::move(moved)});
  }
  void set_space_bars(std::string account, mux::config::space_item_t item, bool side, bool top) {
    requests.emplace_back(request::set_space_bars{std::move(account), std::move(item), side, top});
  }
  void flip_spaces() { requests.emplace_back(request::flip_spaces{}); }
  void flip_top_bar() { requests.emplace_back(request::flip_top_bar{}); }
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
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::start_chat_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::start_chat_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::wallpaper_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::wallpaper_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::packs_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::packs_box<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::create_room_box<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::create_room_box<mux::app::actions>>() noexcept;
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
