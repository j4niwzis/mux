// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:list_shown -- What the chats screen's side column shows, as the
// program says it: the account listed and its folder, the folder tabs and
// the space bars, the forum open, what is searched and found, and the chats
// listed. The program's (mux.app.chat_list) to work out and set; the side
// column reads it and keeps nothing.
export module mux.ui:list_shown;

import std;
import splice;
import skia;
import mux.core;
import mux.config;
import mux.ui.base;
import skiff.scene;
import :chat_list;

export namespace mux::ui {

// What the chat list shows: all the chats, those of a Matrix space, those of
// an XMPP roster group, or the direct chats.
namespace folder {
struct all {
  friend bool operator==(all, all) = default;
};
struct space {
  std::string room;
  friend bool operator==(const space&, const space&) = default;
};
struct group {
  std::string name;
  friend bool operator==(const group&, const group&) = default;
};
struct direct {
  friend bool operator==(direct, direct) = default;
};
}  // namespace folder
using folder_t = spl::variant<folder::all, folder::space, folder::group, folder::direct>;
// The space a folder is, where it is one.
[[nodiscard]] inline std::optional<std::string> space_of(const folder_t& one) {
  return spl::visit(spl::overloaded{[](const folder::space& s) { return std::optional<std::string>(s.room); },
                                    [](const auto&) { return std::optional<std::string>(); }},
                    one);
}

// What the list is of, as the program keeps it: the account listed, its
// folder, a forum gone to by Alt+Up or Alt+Down (lit, not opened), what is
// typed in the search over it, and whether the bars are made big for a
// finger.
struct list_shown {
  std::optional<account_id> account;
  folder_t folder = folder::all{};
  std::optional<conversation_id> pointed;
  std::string search;
  bool big_bars = false;
  // The forum open, its rooms listed (as chat_shown says): with the account
  // and the folder, which list it is.
  std::optional<std::string> forum;
  friend bool operator==(const list_shown&, const list_shown&) = default;
};

// A folder's tab over the list; an icon of a space bar -- Home, Direct
// messages, a space, a space in one (smaller a level down).
struct folder_tab_shown {
  std::string name;
  folder_t folder;
  bool chosen = false;
  friend bool operator==(const folder_tab_shown&, const folder_tab_shown&) = default;
};
struct space_icon_shown {
  config::space_item_t item;
  folder_t folder;
  std::string key;  // its picture's
  std::string name;
  float size = 40.0f;
  bool chosen = false;
  friend bool operator==(const space_icon_shown&, const space_icon_shown&) = default;
};
// The side column, as the program works it out: the tabs (none where there
// is only All), the bars and whether each is up, the forum open's name, and
// "No chats yet" where the account has none listed.
struct side_shown {
  std::vector<folder_tab_shown> folders;
  std::vector<space_icon_shown> side_bar, top_bar;
  bool side_on = false, top_on = false;
  std::optional<std::string> forum;
  bool no_chats = false;
  // The chats' list up: not while messages found are in its place, and not
  // empty while the server's search is shown -- that taking the room.
  bool chats_on = true;
  // An icon being dragged: the side bar shown to drop on, whatever it holds.
  bool dragging = false;
  // Whose spaces the bars hold: what a space's menu asks with.
  std::string account;
  friend bool operator==(const side_shown&, const side_shown&) = default;
};
// The chats screen's frame, as the program keeps it: how wide the side and
// the info are (their own, until their edges are dragged), whether the
// window is one pane at a time (a phone's), and then which is up.
namespace pane {
struct list {
  friend bool operator==(list, list) = default;
};
struct chat {
  friend bool operator==(chat, chat) = default;
};
struct beside {  // the info or the threads, in the chat's place
  friend bool operator==(beside, beside) = default;
};
}  // namespace pane
using pane_t = spl::variant<pane::list, pane::chat, pane::beside>;
struct screen_shown {
  float side_width = 300.0f;
  float info_width = 340.0f;
  bool single = false;
  pane_t pane = pane::list{};
  // What is up, from those: the chats, the chat, its info or its threads
  // beside it (in its place, single), and the edges between them.
  bool side_up = true, chat_up = true, info_up = false, threads_up = false, side_edge = true, beside_edge = false;
  // The panel docked under the field on a phone's window, as high as it is:
  // the chat standing over it.
  float docked = 0.0f;
  // The keys caught before the field takes them, and whether each is live:
  // messages selected (Ctrl+C, Delete), the list over the field (Up, Down,
  // Enter, Tab, Esc), a forum gone to (Alt+Right) or open (Alt+Left).
  bool selecting = false, suggesting = false, pointing = false, in_forum = false;
  friend bool operator==(const screen_shown&, const screen_shown&) = default;
};
// What a search in the chat found, in the chats' place while it is asked:
// how many, and each.
struct found_shown {
  std::optional<std::size_t> count;
  std::vector<search_result> results;
  friend bool operator==(const found_shown&, const found_shown&) = default;
};
// What the server found of what is typed in the side search: rooms and
// people, while the search asks the server -- none where it does not.
struct server_search_facts {
  std::vector<directory_room> rooms;
  std::vector<found_person> people;
  std::optional<account_id> by;
  bool waiting = false;
  friend bool operator==(const server_search_facts&, const server_search_facts&) = default;
};
struct elsewhere_shown {
  std::optional<server_search_facts> found;
  friend bool operator==(const elsewhere_shown&, const elsewhere_shown&) = default;
};
// The chats listed, in order: the program's snapshot, its rows resolving
// their conversations against the chats model; told again with a new
// revision where it moved.
struct listing_shown {
  std::uint64_t revision = 0;
  std::shared_ptr<const chat_listing> listing = std::make_shared<const chat_listing>();
  // What a row's menu offers: the account whose list this is, every
  // account saved (its places), and the theme its strip colours are in.
  std::optional<account_id> account;
  std::vector<account_id> accounts;
  config::theme_t theme = config::theme::tinted{};
  friend bool operator==(const listing_shown& a, const listing_shown& b) { return a.revision == b.revision; }
};

namespace request {
// A folder chosen: a tab's, or a bar's icon's.
struct pick_folder {
  folder_t folder;
};
// What is typed in the side search, as it is typed.
struct list_search {
  std::string text;
};
// The bars made big for a finger, or as they were.
struct big_bars {
  bool on = false;
};
// Out of the forum open, to its row in the list, lit (Alt+Left).
struct leave_forum {};
// Into the forum gone to by Alt+Up or Alt+Down (Alt+Right).
struct open_pointed {};
// A swipe across the chats screen on a phone's window: right, or left.
struct screen_swipe {
  bool right = true;
};
// A key pressed on the chats screen that nothing under the keys took: what
// it does is the program's to say, from what it holds.
struct screen_key {
  skiff::scene::Key key;
  bool control = false, shift = false, alt = false;
};
}  // namespace request

}  // namespace mux::ui
