// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:requests -- What the window asks the program for, each a type of
// its own: sent as events by the window's widgets, taken where they are
// for. And what the message field's text is for.
export module mux.ui:requests;

import std;
import splice;
import skiff.scene;
import mux.core;
import mux.config;
import mux.protocols;

export namespace mux::ui {

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
using compose_t = spl::variant<compose::plain, compose::reply, compose::edit>;

namespace request {
struct choose {
  mux::conversation_id which;
};
struct back {};
struct open_accounts {};
struct open_new_account {};
struct add_account_of {  // a protocol's form, to add an account of it
  mux::protocol_t speaks;
};
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
struct menu_copy_link {};
struct menu_copy_url {};
struct menu_fave_sticker {};
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
// One of the messages found, picked in their list.
struct search_pick {
  std::size_t index = 0;
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
// A space's own settings, as a room's Manage; it shown as a forum, or not;
// the forum open in the chat list left.
struct manage_space {
  std::string room;
};
struct flip_home_hide {
  std::string room;
};
struct flip_forum {
  std::string room;
};
struct close_forum {};
// The settings of the forum open in the list: it is in no bar to be
// right-pressed.
struct manage_forum {};
struct search_rooms {
  std::string server;
  std::string query;
  std::optional<std::string> since;  // the next page of the same search
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
  bool encrypted = true;  // end-to-end, from the start
  // In a space, by its account; its members let in; a space itself.
  std::optional<mux::conversation_id> space;
  bool space_members = false;
  bool make_space = false;
};
// Element's Start chat and Create a room: people searched for, a room's
// box opened and closed; and a text put on the clipboard (one's link).
struct find_people {
  std::string query;
};
// The chat list's search, where nothing joined matches: the server's room
// directory and user directory asked.
struct search_elsewhere {
  std::string query;
};
struct open_new_room {};
// Leaving a space: the box asked for, with which of its rooms to leave; the
// space left, with those; the box closed.
struct open_leave_space {
  mux::conversation_id space;
};
struct leave_space {
  mux::conversation_id space;
  std::vector<std::string> rooms;
};
struct close_leave_space {};
// Create a room -- or a space -- in a space, from its menu.
struct open_new_room_in {
  mux::conversation_id space;
  std::string name;
  bool make_space = false;
};
// Emojis & Stickers: one's own pack, or the room's packs; a pack saved or
// taken away; images chosen for the pack open.
// Threads: their panel opened or closed, one opened or let go (back to the
// list), an answer sent in one, one begun from a message's menu.
// A chat background: its dialog, for a level; what is chosen there.
struct open_wallpaper {
  mux::choice_level_t level;
};
struct close_wallpaper {};
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
// Home without what spaces hold, but direct messages: at a level, or as the
// level over it says.
struct set_home_hides {
  mux::choice_level_t level;
  std::optional<bool> on;
};
// And its direct messages out of Home too: only where Home is so.
struct set_home_direct {
  mux::choice_level_t level;
  std::optional<bool> on;
};
// How much Frosted blurs, in percent.
struct set_frost_blur {
  double percent = 10.0;
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
  std::optional<std::string> reply_to;  // an answer in it answered
};
struct menu_thread {};
struct open_packs {};
struct open_room_packs {};
struct close_packs {};
struct save_pack {
  mux::emote_pack pack;
};
struct delete_pack {
  mux::emote_pack pack;
};
struct pick_pack_images {};
struct close_new_room {};
// A call: made to a chat; the one ringing here taken or turned down; hung
// up; the microphone muted or not.
struct dismiss_call {};
struct start_call {
  mux::conversation_id in;
};
// A call to the chat open, from its header.
struct call_chosen {};
struct accept_call {};
struct decline_call {};
struct hang_up {};
struct mute_call {};
// A text menu's item for a field: the key the field takes for it, given to
// the field with the focus -- Ctrl+V a paste.
struct text_key {
  skiff::scene::Key key;
  bool shift = false;  // with Shift too: Ctrl+Shift+X struck through
};
// Ctrl+K in the message field: the link box, for what is selected.
struct ask_link {};
// The link box done: `text` linked to `url` where the selection was.
struct set_link {
  std::string text;
  std::string url;
};
struct close_link {};
struct copy_text {
  std::string text;
};
struct settings_notifications {};
struct join_room_card {};
// The room card's room asked to be let into.
struct knock_room_card {};
struct decline_room_card {};
struct toggle_emoji {};
// The account chosen in Accounts: its colour, and its strip on its chats in
// other accounts' lists.
struct set_account_colour {
  mux::config::accent_t colour;
};
struct flip_account_strip {};
// The room the chosen one was upgraded to: opened, joined where it is not yet.
struct open_replacement {};
// A chat listed in another account's list too, or moved there; taken out of
// one; its strip there on or off, or its colour.
struct place_chat {
  mux::conversation_id chat;
  mux::account_id to;
  bool moved = false;
};
struct unplace_chat {
  mux::conversation_id chat;
  mux::account_id from;
};
struct flip_chat_strip {
  mux::conversation_id chat;
  mux::account_id in;
};
struct set_chat_strip_colour {
  mux::conversation_id chat;
  mux::account_id in;
  mux::config::accent_t colour;
};
// The thread panel's own: its paperclip, and its emoji button.
struct attach_in_thread {};
struct toggle_thread_emoji {};
// Which field the emoji picker writes in: the chat's, or the thread's.
namespace writing {
struct chat {};
struct thread {};
}  // namespace writing
using writing_t = spl::variant<writing::chat, writing::thread>;
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
// Link previews, at a level: on, off, or (at an account's or a chat's) as
// the level over it says.
// A passphrase given, for what it was asked: the one now, a new one twice.
struct give_passphrase {
  mux::proto::passphrase_for_t why;
  std::string current, fresh, again;
  std::string file;  // a key file's path, where one was asked
};
// Emoji verification: with a person (all their devices), with one of this
// account's sessions; and the answers in its dialog.
struct verify_person {
  mux::conversation_id who;  // the account, and the person's ID
};
struct verify_accept_now {};
struct verify_cancel_now {};
struct verify_match {};
struct verify_mismatch {};
struct close_verification {};
// The chosen account's room keys, to a file or from one.
// Local data's encryption flipped from Storage: a passphrase asked for.
struct flip_local_encryption {};
struct change_passphrase {};
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
// A message's edit history, as mux saw its edits come.
struct menu_edit_history {};
// Messages selected, as tdesktop selects them: from a message's menu, a
// press on one while some are, and what the selection bar does with them.
struct menu_select {};
struct toggle_selected {
  std::string id;
};
struct selection_forward {};
struct selection_copy {};
struct selection_delete {};
struct selection_cancel {};
struct close_dialog {};
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
struct close_edit_history {};
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
// A chat muted or not from its row: a swipe across it, one thing at a time.
struct toggle_mute_of {
  conversation_id which;
};
struct close_account_pages {};
struct accounts_back {};
struct flip_account_receipts {};
struct flip_only_verified {};
// The chosen account's read mentions shared with its other sessions, or
// not; sealed there, or not.
struct flip_account_mentions_shared {};
struct flip_account_mentions_sealed {};
// A person's reset identity accepted, as Element's "Withdraw verification".
struct accept_identity {
  mux::conversation_id who;
};
struct typing {
  bool on = false;
};
struct proxy_kind {
  mux::config::proxy_kind_t kind;
};
struct settings_rendering {};
struct settings_storage {};
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
struct set_renderer {
  mux::config::renderer_t renderer;
};
struct leave_chat {};
// Out of the chat open, to the chats: a swipe across it, single.
struct close_chat {};
struct switch_account {
  std::string address;
};
struct close_settings {};
struct settings_home {};
struct settings_animations {};
}  // namespace request

}  // namespace mux::ui
