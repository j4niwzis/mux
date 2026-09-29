// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:base -- The palette, the protocol of an address, the program asked, and laying out.
export module mux.ui:base;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.widgets.theme;
import mux.core;
import mux.config;
import mux.logic.text;

export namespace mux::ui {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;
namespace widgets = skiff::widgets;

inline skia::SkColor background = skia::colorSetARGB(255, 24, 27, 30);
inline skia::SkColor sidebar_colour = skia::colorSetARGB(255, 32, 36, 40);
inline skia::SkColor chosen_colour = skia::colorSetARGB(255, 52, 60, 66);
inline skia::SkColor text_colour = skia::colorSetARGB(255, 235, 240, 243);
inline skia::SkColor dim_colour = skia::colorSetARGB(255, 150, 162, 170);
inline skia::SkColor accent_colour = skia::colorSetARGB(255, 102, 204, 255);
inline skia::SkColor error_colour = skia::colorSetARGB(255, 255, 120, 110);
// The chosen chat's text, on the chosen colour; one's own bubbles; the
// chat's background, as the theme's wallpaper; text on the accent.
inline skia::SkColor selected_text_colour = skia::colorSetARGB(255, 255, 255, 255);
inline skia::SkColor out_bubble_colour = skia::colorSetARGB(255, 43, 82, 120);
inline skia::SkColor chat_colour = skia::colorSetARGB(255, 14, 22, 33);
inline skia::SkColor on_accent_colour = skia::colorSetARGB(255, 255, 255, 255);

// The protocol an address speaks: a Matrix user ID starts with '@', and a JID
// cannot.
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) {
  return logic::protocol_of(address);
}

// What the screens ask of the program. Each is a request: the program acts on
// it between events.
//
//   void choose(const conversation_id&)
//   void send(const conversation_id&, std::string)
//   void back()                      -- to the conversations
//   void open_accounts()
//   void open_new_account()
//   void add_xmpp()                  -- the XMPP form, when adding
//   void add_matrix()                -- the Matrix form, when adding
//   void select_account(std::string address)
//   void toggle_advanced()
//   void toggle_plain()
//   void submit_login()
//   void flip_enabled(std::string address)
//   void remove_account(std::string address)
//   void open_drawer()
//   void show_account(std::string address)  -- its settings, from the drawer
//   void set_motion(std::string level)       -- "full", "reduced" or "none"
//   void quit()
//   void toggle_mute()               -- the chosen chat muted, or not
//   void leave_chat()                -- the chosen chat left
//   void close_account_pages()       -- back to the list of accounts
//   void choose_new_proxy(int)       -- the proxy of an account being added
//   void accounts_back()              -- ← on the accounts page
//   void account_page(int)           -- a page of the chosen account
//   void flip_account_receipts(), flip_account_typing(), choose_account_proxy(int), manage_proxies()
//   void typing(bool)                -- the composer has text in it, or not
//   void settings_proxies(), add_proxy(), edit_proxy(int), proxy_kind(int),
//        save_proxy_profile(), delete_proxy_profile()
//   void settings_appearance(), settings_rendering(), settings_storage()
//   void change_limit(config::limit_t, bool more), clear_stored()  -- Storage
//   void settings_files(), flip_strip_metadata(), flip_rename_pictures()  -- Files
//   void flip_show_deleted()  -- Storage: deleted messages shown, marked
//   void set_theme(config::theme_t), set_accent(config::accent_t), set_renderer(config::renderer_t)
//   void proxy_kind(config::proxy_kind_t)
//   void not_implemented(std::string what)  -- a box saying it is not there yet
//   void close_notice()
//   void resize_sidebar(float x)     -- the chat list's edge dragged to x
//   void resize_info(float x)        -- the chat info's edge dragged to x
//   void submit_message(std::string text)  -- Enter in the message field
//   void send_typed()                -- the send arrow: what is in the field
//   void toggle_info()               -- the chosen chat's info, beside it
//   void message_person(const conversation_id&)  -- a member's direct chat
//   void jump_to_message(std::string id)  -- a quoted message, scrolled to
//   void message_menu(menu_facts), menu_copy_link(), menu_save(), menu_react(std::string key)  -- a message's menu
//   void react(std::string id, std::string key)  -- a reaction put or taken back
//   void reply_to(std::string id, std::string text)  -- a message swiped left
//   void open_picture(std::string source, std::string sender, std::string name, std::string when)
//   void close_picture(), save_picture(std::string source), open_file(std::string source, std::string name)
//   void attach_files(), close_send_box(), send_files()  -- what is sent with the paperclip
//   void open_member_info(std::string id)  -- a person's info, in the middle
//   void close_person_info()
//   void close_room_card(), join_room_card()  -- a room not joined, from a link: its card
//   void jump_to_mark(mark_kind_t)   -- the oldest unseen mention or reaction, gone to
//   void list_marks(mark_kind_t), go_to_mark(mark_kind_t, std::string event), close_marks()  -- all of them listed
//   void open_explore(), close_explore(), search_rooms(server, query), join_directory_room(room, server),
//        create_room(name, topic, open, alias)  -- rooms found and made
//   void settings_notifications(), flip_notify(notify_flag_t), set_notify_backend(notify_backend_t),
//        flip_account_notify(), flip_account_notify_sound(), set_chat_notify(notify_mode_t)  -- notifications
//   void set_room_event_kind(choice_level_t, optional<room_event_t>, optional<bool>)  -- which room events show
//   void toggle_emoji(), close_emoji(), insert_emoji(std::string)  -- the input's emoji panel
//   void load_older(const conversation_id&, std::string from)  -- its history
//   void jump_to_end()               -- back to a chat's newest message
//   void open_search(), close_search(), search_typed(std::string), search_step(bool older)  -- finding in a chat
//   void open_url(std::string)       -- a link, in the browser
//   void switch_account(std::string address)  -- whose chats are listed
//   void pop_panel()                 -- back from the top panel to what is under it
//   void open_settings(), close_settings(), settings_home(), settings_animations()

// A request with nothing to say but itself: `ask<Actions, &Actions::back>`.
template <class Actions, auto Method>
struct ask {
  Actions* actions = nullptr;
  void operator()() const { (actions->*Method)(); }
};
// The requests about one saved account.
template <class Actions>
struct flip_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->flip_enabled(address); }
};
template <class Actions>
struct remove_account {
  Actions* actions = nullptr;
  std::string address;
  void operator()() const { actions->remove_account(address); }
};

// ---- laying out ----------------------------------------------------------

// Nodes laid out one under another down a column, each followed by its gap;
// the hidden ones take no room.
struct column_stack {
  skia::SkRect column;
  float y = 0.0f;
  template <class Child>
  void operator()(Child& node, float after) {
    if (!node.visible())
      return;
    node.fState.arrange(0.0f, y);
    scene::layout(node, column);
    y += node.bounds().height() + after;
  }
};

// The column a form is laid out in: at most `width` wide, centred.
[[nodiscard]] inline skia::SkRect form_column(const skia::SkRect& box, float width, float top) {
  const float w = std::min(width, box.width() - 32.0f);
  return skia::SkRect::MakeXYWH(box.centerX() - w * 0.5f, box.fTop + top, w, std::max(0.0f, box.height() - top));
}

}  // namespace mux::ui
