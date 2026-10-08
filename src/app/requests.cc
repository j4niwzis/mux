// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.requests: What the window asks: requests, and the actions that make them.
export module mux.app.requests;

import std;
import splice;
import skiff.scene;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.protocols;
import mux.app.network;

export namespace mux::app {

// The requests and what the message field is for are the window's own
// (mux.ui:requests): named here as they were.
namespace compose = mux::ui::compose;
using mux::ui::compose_t;
namespace request = mux::ui::request;
using mux::ui::request_t;

// Every request, one of them: a spl::variant, built in time linear in how
// many there are (std::variant's nested union made it quadratic).

// What the screens ask: each a request, kept until the program applies it
// between events -- except a message, which goes to the network at once.
struct actions {
  network* net = nullptr;
  // In the demo, a message sent is there at once, as sent.
  bool demo = false;
  mailbox_type* box = nullptr;
  int demo_sent = 0;
  std::vector<request_t> requests;
  // A request the window sent as an event, which nothing in it took: taken
  // as the program's sink, queued as one asked.
  template <class E>
    requires std::constructible_from<request_t, E>
  void take(const E& one) {
    requests.emplace_back(one);
  }
  // A protocol's "nothing to ask": nothing.
  void take(const mux::proto::part::no_request&) {}

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
  void add_account_of(mux::protocol_t speaks) { requests.emplace_back(request::add_account_of{speaks}); }
  void select_account(std::string address) { requests.emplace_back(request::select_account{std::move(address)}); }
  void toggle_advanced() { requests.emplace_back(request::toggle_advanced{}); }
  void toggle_plain() { requests.emplace_back(request::toggle_plain{}); }
  void submit_login() { requests.emplace_back(request::submit_login{}); }
  void flip_enabled(std::string address) { requests.emplace_back(request::flip_enabled{std::move(address)}); }
  void remove_account(std::string address) { requests.emplace_back(request::remove_account{std::move(address)}); }
  void open_drawer() { requests.emplace_back(request::open_drawer{}); }
  void show_account(std::string address) { requests.emplace_back(request::show_account{std::move(address)}); }
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
  void menu_copy_url() { requests.emplace_back(request::menu_copy_url{}); }
  void menu_fave_sticker() { requests.emplace_back(request::menu_fave_sticker{}); }
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
  void search_pick(std::size_t index) { requests.emplace_back(request::search_pick{index}); }
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
  void manage_space(std::string room) { requests.emplace_back(request::manage_space{std::move(room)}); }
  void flip_forum(std::string room) { requests.emplace_back(request::flip_forum{std::move(room)}); }
  void flip_home_hide(std::string room) { requests.emplace_back(request::flip_home_hide{std::move(room)}); }
  void close_forum() { requests.emplace_back(request::close_forum{}); }
  void manage_forum() { requests.emplace_back(request::manage_forum{}); }
  void search_rooms(std::string server, std::string query) {
    requests.emplace_back(request::search_rooms{std::move(server), std::move(query)});
  }
  void more_rooms(std::string server, std::string query, std::string since) {
    requests.emplace_back(request::search_rooms{std::move(server), std::move(query), std::move(since)});
  }
  void join_directory_room(std::string room, std::string server) {
    requests.emplace_back(request::join_directory_room{std::move(room), std::move(server)});
  }
  void create_room(std::string name, std::string topic, bool open, std::string alias, bool federate, bool encrypted,
                   std::optional<mux::conversation_id> space = std::nullopt, bool space_members = false, bool make_space = false) {
    requests.emplace_back(request::create_room{std::move(name), std::move(topic), open, std::move(alias), federate, encrypted,
                                               std::move(space), space_members, make_space});
  }
  void find_people(std::string query) { requests.emplace_back(request::find_people{std::move(query)}); }
  void search_elsewhere(std::string query) { requests.emplace_back(request::search_elsewhere{std::move(query)}); }
  void open_new_room() { requests.emplace_back(request::open_new_room{}); }
  void open_leave_space(mux::conversation_id space) { requests.emplace_back(request::open_leave_space{std::move(space)}); }
  void leave_space(mux::conversation_id space, std::vector<std::string> rooms) {
    requests.emplace_back(request::leave_space{std::move(space), std::move(rooms)});
  }
  void close_leave_space() { requests.emplace_back(request::close_leave_space{}); }
  void open_new_room_in(mux::conversation_id space, std::string name, bool make_space) {
    requests.emplace_back(request::open_new_room_in{std::move(space), std::move(name), make_space});
  }
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
  void send_in_thread(std::string root, std::string text, std::optional<std::string> reply_to) {
    requests.emplace_back(request::send_in_thread{std::move(root), std::move(text), std::move(reply_to)});
  }
  void menu_thread() { requests.emplace_back(request::menu_thread{}); }
  void open_room_packs() { requests.emplace_back(request::open_room_packs{}); }
  void close_packs() { requests.emplace_back(request::close_packs{}); }
  void save_pack(mux::emote_pack pack) { requests.emplace_back(request::save_pack{std::move(pack)}); }
  void delete_pack(mux::emote_pack pack) {
    requests.emplace_back(request::delete_pack{std::move(pack)});
  }
  void pick_pack_images() { requests.emplace_back(request::pick_pack_images{}); }
  void close_new_room() { requests.emplace_back(request::close_new_room{}); }
  void copy_text(std::string text) { requests.emplace_back(request::copy_text{std::move(text)}); }
  void text_key(skiff::scene::Key key, bool shift = false) { requests.emplace_back(request::text_key{key, shift}); }
  void ask_link() { requests.emplace_back(request::ask_link{}); }
  void set_link(std::string text, std::string url) { requests.emplace_back(request::set_link{std::move(text), std::move(url)}); }
  void close_link() { requests.emplace_back(request::close_link{}); }
  void start_call(mux::conversation_id in) { requests.emplace_back(request::start_call{std::move(in)}); }
  void call_chosen() { requests.emplace_back(request::call_chosen{}); }
  void dismiss_call() { requests.emplace_back(request::dismiss_call{}); }
  void accept_call() { requests.emplace_back(request::accept_call{}); }
  void decline_call() { requests.emplace_back(request::decline_call{}); }
  void hang_up() { requests.emplace_back(request::hang_up{}); }
  void mute_call() { requests.emplace_back(request::mute_call{}); }
  void settings_notifications() { requests.emplace_back(request::settings_notifications{}); }
  void join_room_card() { requests.emplace_back(request::join_room_card{}); }
  void knock_room_card() { requests.emplace_back(request::knock_room_card{}); }
  void decline_room_card() { requests.emplace_back(request::decline_room_card{}); }
  void toggle_emoji() { requests.emplace_back(request::toggle_emoji{}); }
  void set_account_colour(mux::config::accent_t colour) { requests.emplace_back(request::set_account_colour{colour}); }
  void flip_account_strip() { requests.emplace_back(request::flip_account_strip{}); }
  void open_replacement() { requests.emplace_back(request::open_replacement{}); }
  void place_chat(mux::conversation_id chat, mux::account_id to, bool moved) {
    requests.emplace_back(request::place_chat{std::move(chat), std::move(to), moved});
  }
  void unplace_chat(mux::conversation_id chat, mux::account_id from) {
    requests.emplace_back(request::unplace_chat{std::move(chat), std::move(from)});
  }
  void flip_chat_strip(mux::conversation_id chat, mux::account_id in) {
    requests.emplace_back(request::flip_chat_strip{std::move(chat), std::move(in)});
  }
  void set_chat_strip_colour(mux::conversation_id chat, mux::account_id in, mux::config::accent_t colour) {
    requests.emplace_back(request::set_chat_strip_colour{std::move(chat), std::move(in), colour});
  }
  void toggle_thread_emoji() { requests.emplace_back(request::toggle_thread_emoji{}); }
  void attach_in_thread() { requests.emplace_back(request::attach_in_thread{}); }
  void close_emoji() { requests.emplace_back(request::close_emoji{}); }
  void menu_save_gif() { requests.emplace_back(request::menu_save_gif{}); }
  void menu_pin() { requests.emplace_back(request::menu_pin{}); }
  void menu_reactions() { requests.emplace_back(request::menu_reactions{}); }
  void open_manage() { requests.emplace_back(request::open_manage{}); }
  void menu_forward() { requests.emplace_back(request::menu_forward{}); }
  void menu_view_source() { requests.emplace_back(request::menu_view_source{}); }
  void menu_edit_history() { requests.emplace_back(request::menu_edit_history{}); }
  void menu_select() { requests.emplace_back(request::menu_select{}); }
  void toggle_selected(std::string id) { requests.emplace_back(request::toggle_selected{std::move(id)}); }
  void selection_forward() { requests.emplace_back(request::selection_forward{}); }
  void selection_copy() { requests.emplace_back(request::selection_copy{}); }
  void selection_delete() { requests.emplace_back(request::selection_delete{}); }
  void selection_cancel() { requests.emplace_back(request::selection_cancel{}); }
  void close_dialog() { requests.emplace_back(request::close_dialog{}); }
  void open_new_chat() { requests.emplace_back(request::open_new_chat{}); }
  void close_new_chat() { requests.emplace_back(request::close_new_chat{}); }
  void start_direct(std::string user) { requests.emplace_back(request::start_direct{std::move(user)}); }
  void start_group(std::string name) { requests.emplace_back(request::start_group{std::move(name)}); }
  void close_forward() { requests.emplace_back(request::close_forward{}); }
  void forward_to(mux::conversation_id to) { requests.emplace_back(request::forward_to{std::move(to)}); }
  void flip_room_events() { requests.emplace_back(request::flip_room_events{}); }
  void give_passphrase(mux::proto::passphrase_for_t why, std::string current, std::string fresh, std::string again,
                       std::string file) {
    requests.emplace_back(request::give_passphrase{why, std::move(current), std::move(fresh), std::move(again), std::move(file)});
  }
  void verify_person(mux::conversation_id who) { requests.emplace_back(request::verify_person{std::move(who)}); }
  void verify_accept_now() { requests.emplace_back(request::verify_accept_now{}); }
  void verify_cancel_now() { requests.emplace_back(request::verify_cancel_now{}); }
  void verify_match() { requests.emplace_back(request::verify_match{}); }
  void verify_mismatch() { requests.emplace_back(request::verify_mismatch{}); }
  void close_verification() { requests.emplace_back(request::close_verification{}); }
  void flip_local_encryption() { requests.emplace_back(request::flip_local_encryption{}); }
  void change_passphrase() { requests.emplace_back(request::change_passphrase{}); }
  void flip_account_room_events() { requests.emplace_back(request::flip_account_room_events{}); }
  void flip_chat_room_events() { requests.emplace_back(request::flip_chat_room_events{}); }
  void close_manage() { requests.emplace_back(request::close_manage{}); }
  void room_act(mux::room_action_t action) { requests.emplace_back(request::room_act{std::move(action)}); }
  void close_reactions() { requests.emplace_back(request::close_reactions{}); }
  void close_edit_history() { requests.emplace_back(request::close_edit_history{}); }
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
  void toggle_mute_of(conversation_id which) { requests.emplace_back(request::toggle_mute_of{std::move(which)}); }
  void close_account_pages() { requests.emplace_back(request::close_account_pages{}); }
  void accounts_back() { requests.emplace_back(request::accounts_back{}); }
  void account_page(mux::ui::account_page_t page) { requests.emplace_back(request::account_page{std::move(page)}); }
  void flip_account_receipts() { requests.emplace_back(request::flip_account_receipts{}); }
  void flip_only_verified() { requests.emplace_back(request::flip_only_verified{}); }
  void flip_account_mentions_shared() { requests.emplace_back(request::flip_account_mentions_shared{}); }
  void flip_account_mentions_sealed() { requests.emplace_back(request::flip_account_mentions_sealed{}); }
  void accept_identity(mux::conversation_id who) { requests.emplace_back(request::accept_identity{std::move(who)}); }
  // A protocol's own request, as its UI asks it (mux::ui::asks).
  template <class Request>
  void ask_for(Request one) {
    requests.emplace_back(std::move(one));
  }
  void typing(bool on) { requests.emplace_back(request::typing{on}); }
  void proxy_kind(mux::config::proxy_kind_t kind) { requests.emplace_back(request::proxy_kind{kind}); }
  void settings_rendering() { requests.emplace_back(request::settings_rendering{}); }
  void settings_storage() { requests.emplace_back(request::settings_storage{}); }
  void clear_stored() { requests.emplace_back(request::clear_stored{}); }
  void choose_account_proxy(int index) { requests.emplace_back(request::choose_account_proxy{index}); }
  void manage_proxies() { requests.emplace_back(request::manage_proxies{}); }
  void settings_proxies() { requests.emplace_back(request::settings_proxies{}); }
  void add_proxy() { requests.emplace_back(request::add_proxy{}); }
  void edit_proxy(int index) { requests.emplace_back(request::edit_proxy{index}); }
  void save_proxy_profile() { requests.emplace_back(request::save_proxy_profile{}); }
  void delete_proxy_profile() { requests.emplace_back(request::delete_proxy_profile{}); }
  void settings_appearance() { requests.emplace_back(request::settings_appearance{}); }
  void set_renderer(mux::config::renderer_t renderer) { requests.emplace_back(request::set_renderer{renderer}); }
  void set_frost_blur(double percent) { requests.emplace_back(request::set_frost_blur{percent}); }
  void place_spaces(std::string account, mux::config::space_bar_t bar, std::vector<mux::config::space_item_t> order,
                    std::optional<mux::config::space_bar_t> from, std::optional<mux::config::space_item_t> moved) {
    requests.emplace_back(request::place_spaces{std::move(account), bar, std::move(order), from, std::move(moved)});
  }
  void set_space_bars(std::string account, mux::config::space_item_t item, bool side, bool top) {
    requests.emplace_back(request::set_space_bars{std::move(account), std::move(item), side, top});
  }
  void set_home_hides(mux::choice_level_t level, std::optional<bool> on) {
    requests.emplace_back(request::set_home_hides{level, on});
  }
  void set_home_direct(mux::choice_level_t level, std::optional<bool> on) {
    requests.emplace_back(request::set_home_direct{level, on});
  }
  void leave_chat() { requests.emplace_back(request::leave_chat{}); }
  void close_chat() { requests.emplace_back(request::close_chat{}); }
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
// The chats screen cut further: its heaviest subtrees each in a unit of
// their own, compiled side by side -- the one unit was six minutes.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen<mux::app::actions>::side_column> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>::side_column>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen<mux::app::actions>::chat_column> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen<mux::app::actions>::chat_column>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::message_bubble<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::message_bubble<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::composer_bar<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::composer_bar<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::info_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::info_panel<mux::app::actions>>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::threads_panel<mux::app::actions>> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::threads_panel<mux::app::actions>>() noexcept;
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
