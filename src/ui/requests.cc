// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:requests -- What the window asks the program for, each a type of
// its own: sent as events by the window's widgets, taken where they are
// for. And what the message field's text is for.
export module mux.ui.base:requests;

import std;
import splice;
import skiff.scene;
import mux.core;
import mux.config;
import mux.protocols;
import mux.logic.links;
import mux.plugins.types;
import mux.composition;
import skiff.bind;
import skiff.model;

export namespace mux::ui {


// Lists of types, put together: the client's and every protocol's -- the
// Manage tabs and pages, the account pages.
template <class... Ts>
struct type_list {};
template <class... Lists>
struct joined;
template <class... Ts>
struct joined<type_list<Ts...>> {
  using type = type_list<Ts...>;
};
template <class... As, class... Bs, class... Rest>
struct joined<type_list<As...>, type_list<Bs...>, Rest...> : joined<type_list<As..., Bs...>, Rest...> {};
template <class List>
struct variant_of_types;
template <class... Ts>
struct variant_of_types<type_list<Ts...>> {
  using type = spl::variant<Ts...>;
};

// One who has read a message: who, by their name in the chat, and when,
// where their receipt says.
struct seen_reader {
  std::string id;
  std::string name;
  std::optional<std::chrono::sys_time<std::chrono::milliseconds>> at;
  friend bool operator==(const seen_reader&, const seen_reader&) = default;
};
struct menu_facts {
  std::string id;
  bool own = false;
  std::string text;    // all of it
  std::string copied;  // what Copy takes: the selection, or all of it
  std::optional<skiff::scene::ClipboardFragment> copied_fragment;
  bool selection = false;
  std::vector<seen_reader> seen;
  std::optional<std::string> media;  // a picture's or a file's source
  std::optional<std::string> picture;  // a picture's source, or a video's thumbnail's: what Copy Image copies
  bool captioned = false;  // a picture whose caption may be edited (not a video's)
  std::string media_name;
  bool moving = false;  // a GIF or a moving WebP: one that can be saved to the GIFs
  bool pinned = false;  // pinned in its chat: the menu offers Unpin
  bool pinnable = false;  // in a chat where pins are kept: a Matrix room
  bool editable = false;  // one's own, as its protocol's rule for edits allows
  bool history = false;   // edited here before: its edit history can be shown
  proto::account_ops can;  // what its account does: React, Forward, threads...
  bool deletable = false;  // one may take it away: one's own, or another's with the power to
  bool answerable = true;  // may be replied to: on the server, and not deleted
  bool reaction_events = false;  // reacted to, the reactions being events
  std::size_t reaction_count = 0;  // how many reactions it has, of anyone
  std::string link;  // a link to it, where it has one
  std::string pressed_link;  // the link pressed on: in its text, or its preview
  std::optional<message> event;  // a known history/panel event not held in the timeline
  std::optional<emote> emoji;
  std::optional<emote> sticker;  // a sticker's: what making it a favourite keeps
  // A reaction's: the message it is on, and its key -- the menu's reactions
  // change it to another, where it is one's own.
  struct reaction_facts {
    std::string to;
    std::string key;
    friend bool operator==(const reaction_facts&, const reaction_facts&) = default;
  };
  std::optional<reaction_facts> reaction;
  float x = 0.0f, y = 0.0f;
  friend bool operator==(const menu_facts&, const menu_facts&) = default;
};
// Compared, or the window's layer (skiff.compose's Mounted) makes the menu
// anew at every refresh -- a press in it is one -- and what the menu has
// open of its own is lost: the arrow over its quick reactions unrolled the
// emoji, and the menu made again at once had them rolled up (the user's:
// "I can't even open it").
static_assert(std::equality_comparable<menu_facts>);
namespace request {
// A message's menu asked for: what it is opened over.
using message_menu = menu_facts;
}  // namespace request

// The client's pages of an account's settings, every account's.
namespace account_page {
struct appearance { friend bool operator==(appearance, appearance) = default; };
struct connection {};
struct privacy {};
struct notifications {};
struct chats {};
}  // namespace account_page
// Each protocol's own pages, as its account_pages(state) lists them.
template <class List>
struct account_page_types;
template <class... Pages>
struct account_page_types<proto::account_page_list<Pages...>> {
  using type = type_list<Pages...>;
};
template <class>
struct protocol_account_pages;
template <class... Tags>
struct protocol_account_pages<protocol_list<Tags...>> {
  using type = typename joined<type_list<>,
                               typename account_page_types<decltype(proto::account_pages_of(::mux::state_of<Tags>{}))>::type...>::type;
};
// Each plugin's own pages, as its account_pages lists them
// (contract::account_pages).
template <class List>
struct plugin_page_types;
template <class... Pages>
struct plugin_page_types<composition::page_list<Pages...>> {
  using type = type_list<Pages...>;
};
template <class>
struct plugin_account_pages;
template <class... Features>
struct plugin_account_pages<plugin_list<Features...>> {
  using type = typename joined<type_list<>,
                               typename plugin_page_types<decltype(account_pages(contract::account_pages{}, type_tag<Features>{}))>::type...>::type;
};
using plugin_account_pages_t = typename plugin_account_pages<composition::providers<contract::account_pages, enabled_plugins>>::type;
// A page of an account's: the client's, one of a protocol's, or a plugin's.
using account_page_t = typename variant_of_types<typename joined<
    type_list<account_page::connection, account_page::privacy, account_page::notifications, account_page::chats,
              account_page::appearance>,
    typename protocol_account_pages<protocols>::type, plugin_account_pages_t>::type>::type;
// What a protocol keeps of an account while its pages are open, as its
// account_parts(state) lists them: one of them all, or none.
template <class List>
struct account_part_types;
template <class... Parts>
struct account_part_types<proto::account_part_list<Parts...>> {
  using type = type_list<Parts...>;
};
template <class>
struct protocol_account_parts;
template <class... Tags>
struct protocol_account_parts<protocol_list<Tags...>> {
  using type = typename joined<type_list<proto::no_account_part>,
                               typename account_part_types<decltype(proto::account_parts_of(::mux::state_of<Tags>{}))>::type...>::type;
};
using account_part_t = typename variant_of_types<typename protocol_account_parts<protocols>::type>::type;

// An account form's draft: what its fields hold as typed, each protocol's
// own (draft_of(kept), account_from(draft) in its kept module) -- the form
// keeps it as it is typed; the program turns it into settings.
template <class Tag>
using draft_of_t = decltype(draft_of(std::declval<const config::kept_of<Tag>&>()));
template <class>
struct protocol_drafts;
template <class... Tags>
struct protocol_drafts<protocol_list<Tags...>> {
  using type = spl::variant<draft_of_t<Tags>...>;
};
using account_draft_t = typename protocol_drafts<protocols>::type;
// What a protocol keeps beside the draft, as its form_parts(state) lists.
template <class List>
struct form_part_types;
template <class... Parts>
struct form_part_types<proto::form_part_list<Parts...>> {
  using type = type_list<Parts...>;
};
template <class>
struct protocol_form_parts;
template <class... Tags>
struct protocol_form_parts<protocol_list<Tags...>> {
  using type = typename joined<type_list<proto::no_form_part>,
                               typename form_part_types<decltype(proto::form_parts_of(::mux::state_of<Tags>{}))>::type...>::type;
};
using form_part_t = typename variant_of_types<typename protocol_form_parts<protocols>::type>::type;
// What an account form ends with saying: what went wrong, or what is
// happening.
struct form_said {
  std::string text;
  bool error = false;
  friend bool operator==(const form_said&, const form_said&) = default;
};
// An account form, as the program shows it: the draft it is made from --
// an account's own, or a protocol's blank one -- made again where the round
// moves; the account it edits, by the address it was kept under, where it
// edits one; what it says; and what its protocol keeps beside it.
struct account_form_facts {
  account_draft_t seed;
  std::optional<std::string> editing;
  std::uint32_t round = 0;
  form_said said;
  form_part_t part;
  // Changed whole, as one part.
  using ModelOpaque = void;
};
namespace request {
// The form sent: its draft as typed, for the program to make settings of.
struct submit_account {
  account_draft_t draft;
};
}  // namespace request
namespace request {
struct apply_style {};
struct configure_theme { config::theme_t theme; std::optional<config::custom_theme> custom; };
struct set_developer_tools { bool enabled; };
// And turned the other way.
struct flip_developer_tools {};
// One of the chosen account's pages asked for.
struct account_page {
  account_page_t page = ::mux::ui::account_page::connection{};
};
}  // namespace request

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
struct refresh_profile { account_id by; };
struct save_profile { account_id by; profile_edit edit; };
struct save_presence { account_id by; mux::presence now; };
struct pick_profile_banner { account_id by; };
struct remove_profile_banner { account_id by; };
struct flip_enabled {
  std::string address;
};
struct remove_account {
  std::string address;
};
struct open_drawer {};
// The drawer pushed back: Esc, under every dialog.
struct close_drawer {};
struct show_account {
  std::string address;
};
struct quit {};
struct open_settings {};
struct pop_panel {};
struct toggle_info {};
// The info's members near the end of those made: the next few.
struct info_more {};
struct jump_to_end {};
// A picture copied to the clipboard: the one a message's menu is up for,
// or one by its source (the viewer's).
struct menu_copy_image {};
struct copy_picture {
  std::string source;
};
struct return_to_chat {};
// The chat's head pressed, off its buttons: its info, where the chat is
// shown one thing at a time.
struct head_pressed {};
// The send box's Add more: other files chosen, to go with those in it --
// a gallery -- its caption as written so far kept.
struct attach_more {
  std::string caption;
};
struct menu_copy_link {};
struct menu_copy_url {};
struct favourite_emoji { emote emoji; };
struct react {
  std::string id;
  std::string key;
  std::optional<std::string> shortcode;
};
struct menu_react {
  std::string key;
  std::optional<std::string> shortcode;
};
struct menu_save {};
struct close_menu {};
// A right press off an open menu: it closed, and the press given again to
// what is under it -- another message's menu opened there, as a desktop's.
struct menu_pressed_off {};
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
// Settings, About: the log kept (mux::kept_log) put on the clipboard.
struct copy_log {};
struct load_context {
  mux::conversation_id in;
  std::string target;
};
struct load_newer {
  mux::conversation_id in;
  std::string from;
};
// Older history: from the token the chat pages back from, or -- where it
// has none, though it holds messages (a room quiet since the program
// started, whose kept timeline lost its token as it was cut short) -- from
// before the oldest message held, wherever that is found.
struct load_older {
  mux::conversation_id in;
  std::optional<std::string> from;
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
// The files in the send box sent, with the caption written there.
struct send_files {
  std::string caption;
};
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
  // Ctrl+Up held: a repeat of the key, not its first press.
  bool repeat = false;
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
// A room not joined, from a link: its card, looking it up; an invite
// chosen: its card, not a chat -- the room card plugin's.
struct look_up_room {
  mux::logic::link_step::join step;
  mux::logic::link_t link;
};
struct show_invite {
  mux::conversation_id chat;
};
// A chat marked read, as a notification's action asks.
struct mark_chat_read {
  mux::conversation_id chat;
};
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
  std::optional<account_id> by;
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
struct open_room_invite { mux::conversation_id room; };
struct invite_to_room { mux::conversation_id room; std::string user; };
// Element's Start chat and Create a room: people searched for, a room's
// box opened and closed; and a text put on the clipboard (one's link).
struct find_people {
  std::string query;
};
// The chat list's search, where nothing joined matches: the server's room
// directory and user directory asked.
struct search_elsewhere {
  std::string query;
  std::optional<account_id> by;
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
// Emojis & Stickers: a room's pack added to the account, from the emoji
// panel -- taken by the packs plugin.
// Threads: their panel opened or closed, one opened or let go (back to the
// list), an answer sent in one, one begun from a message's menu.
// A chat background: its dialog, for a level; what is chosen there.
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
// A look changed by its choices -- a kind picked, a slider let go, an
// element given back to the bubbles' -- at a level: the program works the
// look out of what it holds (bubbles_asked) and sets it.
namespace bubbles_change {
// A kind; none: as the level over it.
struct kind {
  std::optional<mux::config::bubbles_t> to;
};
// How opaque, and Frosted's blur: where along their sliders.
struct opacity {
  float at = 0.0f;
};
struct blur {
  float at = 0.0f;
};
// An element's opacity or blur, apart from the bubbles' (where along its
// slider), or given back to them (none). The bubbles' only.
template <class Which>
struct element {
  Which which;
  std::optional<float> at;
};
}  // namespace bubbles_change
using element_opacity_t = std::optional<int> mux::config::element_opacity::*;
using element_blur_t = std::optional<double> mux::config::element_blur::*;
using bubbles_change_t = spl::variant<bubbles_change::kind, bubbles_change::opacity, bubbles_change::blur,
                                      bubbles_change::element<element_opacity_t>, bubbles_change::element<element_blur_t>>;
struct change_bubbles {
  mux::choice_level_t level;
  mux::config::look_part_t part = mux::config::look_part::bubbles{};
  bubbles_change_t change;
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
  std::vector<styled_run> styles;
};
struct menu_thread {};
struct adopt_pack {
  mux::emote_pack pack;
  std::optional<mux::account_id> by;
};
// A text menu's item for a field: the key the field takes for it, given to
// the field with the focus -- Ctrl+V a paste.
struct text_key {
  skiff::scene::Key key;
  bool shift = false;  // with Shift too: Ctrl+Shift+X struck through
  bool control = true;
};
// Which room events show at a level, set to a way: as above, all, messages
// only, or custom -- from what was in effect.
struct set_events_way {
  mux::choice_level_t level;
  std::size_t way = 0;
};
// Esc on the window, something up to let go of: what, the program says.
struct escape {};
// Ctrl+C, nothing under the keys having taken it: what any text shows
// selected copied.
struct copy_selected {};
// The pointer pressed, where: where a menu that press asks for is put.
struct pressed_at {
  float x = 0.0f, y = 0.0f;
};
// The text menu let go of: pressed off, Esc, or done.
struct close_text_menu {};
struct text_formatting {
  bool open = true;
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
// The room card's room asked to be let into.
// The input's emoji button pressed: where it is laid out, the panel opened
// beside it.
// The chat's list, as it comes to say it: whether its way down is wanted --
// the reader away from its end -- and how many came meanwhile.
struct way_down {
  bool wanted = false;
  std::size_t unseen = 0;
};
struct toggle_emoji {
  float right = 0.0f;
  float top = 0.0f;
};
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
struct toggle_thread_emoji {
  float right = 0.0f;
  float top = 0.0f;
};
}  // namespace request
// Which field the program gives the keys to, and each time it does: a count
// it raises -- the field asked reads it and takes the keys (focused_by); the
// others read 0. One target at a time, so only one field asks.
namespace keys {
struct none {
  friend bool operator==(none, none) = default;
};
struct chat {
  friend bool operator==(chat, chat) = default;
};
struct thread {
  friend bool operator==(thread, thread) = default;
};
struct search {
  friend bool operator==(search, search) = default;
};
struct menu {
  friend bool operator==(menu, menu) = default;
};
}  // namespace keys
using keys_target = spl::variant<keys::none, keys::chat, keys::thread, keys::search, keys::menu>;
struct keys_asked {
  keys_target which = keys::none{};
  std::uint64_t count = 0;
  friend bool operator==(const keys_asked&, const keys_asked&) = default;
};
// What a field of Target reads of it: the count, where the keys are asked
// for it; 0, where not.
template <class Target> struct keys_on {
  std::uint64_t operator()(const keys_asked& now) const {
    return spl::visit(spl::overloaded{[&](Target) { return now.count; }, [](const auto&) { return std::uint64_t{0}; }}, now.which);
  }
};
namespace request {
// Which field the emoji picker writes in: the chat's, or the thread's.
namespace writing {
struct chat {};
struct thread {};
}  // namespace writing
using writing_t = spl::variant<writing::chat, writing::thread>;
// Where the settings page has come to rest, scrolled: kept by the program,
// the page put back there when it is made again.
struct settings_scrolled {
  float at = 0.0f;
};
struct close_emoji {};
struct insert_emoji {
  std::string text;
  std::string picture;  // a custom emoji's, where it is one
};
// The GIFs: one saved from its menu, the saved ones shown in the input's
// panel, and one of them sent.
// A file shown, its whole kept where it is asked to be -- a GIF among the
// saved ones -- from the disk where it is kept once it has loaded; said as
// the asker words it.
struct keep_whole {
  std::string source;
  std::string to;  // the file it is written to
  std::string title, not_loaded, kept;
};
// A message's reactions as the events they are, in a window of their own.
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
struct start_direct {
  std::string user;
  // By which account: the one listed where none is said.
  std::optional<mux::account_id> by{};
};
struct start_group {
  std::string name;
};
// A message's edit history, as mux saw its edits come.
// Messages selected, as tdesktop selects them: from a message's menu, a
// press on one while some are, and what the selection bar does with them.
struct menu_select {};
struct toggle_selected {
  std::string id;
};
struct selection_copy {};
struct selection_delete {};
struct selection_delete_confirm {};
struct selection_span {
  std::vector<std::string> ids;
  bool selected = true;
};
struct selection_cancel {};
// A plugin's action on the messages selected (contract::selection_actions):
// asked by its button, and the messages handed to its part.
template <class Feature>
struct selection_asked {};
template <class Feature>
struct selection_taken {
  mux::conversation_id in;
  std::vector<std::string> events;
};
struct open_manage {};
struct pick_room_avatar { conversation_id room; };
struct close_manage {};
struct room_act {
  mux::room_action_t action;
};
// A plugin's dialog closed (contract::window_dialogs): by its facts, the part
// of what is shown it is; taken by its plugin's part.
template <class Facts>
struct close_shown {};
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
// A sound playing: the program asked to look at the speaker again -- where
// it is, whether it ended -- a few times a second, while one does.
struct audio_tick {
  friend bool operator==(audio_tick, audio_tick) = default;
};
// The proxy profile an account being added goes through, by its name: none
// for none.
struct choose_new_proxy {
  std::optional<std::string> name;
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
struct settings_about {};
struct settings_library { std::string name; };
struct settings_animations {};
// A chat's banner's button pressed: what it asks is its protocol's, asked
// again where the program knows which protocol it is.
struct banner_pressed {
  conversation_id in;
};
// The banner over the chat's composer: its button, or its close -- of the
// chat shown.
struct banner_button {};
struct banner_close {};
// The list over the composer -- the @ list, the : list: the next or the
// one before lit, one picked (the lit one, where none is said), or the
// list closed until something else is asked.
struct suggestion_step {
  bool down = true;
};
struct suggestion_pick {
  std::optional<std::size_t> at;
};
struct suggestion_close {};
// The thread's field: what is written sent in the thread open, an answer to
// what is answered there; the answer let go.
struct thread_send {};
struct thread_answer_cancel {};
// The video in the viewer: paused or played on; and gone to a share of it.
struct toggle_video {};
struct video_seek {
  float share = 0.0f;
};
// One of a person's card's buttons of its protocol's own: by where it is
// among them.
struct card_action {
  account_id by;
  std::string who;
  std::size_t index = 0;
};
}  // namespace request

}  // namespace mux::ui
