// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:base -- The palette, the protocol of an address, the program asked, and laying out.
export module mux.ui:base;

import std;
import splice;
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
// The chat's wallpaper, as Telegram's are: a gradient from the top down to
// chat_colour at the bottom.
inline skia::SkColor chat_top_colour = skia::colorSetARGB(255, 22, 38, 58);
// How the bubbles of the chat shown look: set before its bubbles are made.
inline config::bubble_look& bubble_look_now() {
  static config::bubble_look now;
  return now;
}
// An element's opacity in a look, in percent: its own, else the bubbles'
// -- all of it where they are solid.
[[nodiscard]] inline int element_opacity_of(const config::bubble_look& look, std::optional<int> config::element_opacity::* which) {
  if (const auto& own = look.elements.*which)
    return *own;
  return splice::visit(splice::overloaded{[](config::bubbles::solid) { return 100; }, [&](const auto&) { return look.opacity; }},
                       look.kind);
}
// A menu's plate: the side's colour, all but opaque -- never taken for a
// panel's fill, so a see-through panel look leaves menus readable over it.
[[nodiscard]] inline skia::SkColor popup_colour() { return (sidebar_colour & 0x00FFFFFFu) | (0xFEu << 24); }
// A colour at an opacity in percent.
[[nodiscard]] inline skia::SkColor at_opacity(skia::SkColor colour, int percent) {
  const auto alpha = static_cast<unsigned>(std::lround(((colour >> 24) & 0xFF) * std::clamp(percent, 0, 100) / 100.0));
  return (colour & 0x00FFFFFFu) | (alpha << 24);
}
// The window's look: its opacity in percent in effect (as at the start),
// the one chosen for the next start, and whether the chat's background is
// behind all of the window.
struct window_look_t {
  int opacity = 100;
  int chosen = 100;
  bool behind = false;
  bool see_through = false;  // the window made with an alpha channel: opacity changes at once
  int frost = 30;            // how much Frosted blurs, in percent
  bool spaces = true;        // the space bars at all
  bool top_bar = true;       // the one along the top
  bool home_hides = false;   // Home without what spaces hold, for the client
  bool home_direct = false;  // and without direct messages
};
inline window_look_t& window_look() {
  static window_look_t look;
  return look;
}
// Every chat's bubbles, as chosen for the client.
inline config::bubble_look& bubble_look_everywhere() {
  static config::bubble_look look;
  return look;
}
// The items of the space bars of the account shown, and where each is: for
// the settings to list them. Said by the chat list as it shows them.
struct space_item_shown {
  config::space_item_t item;
  std::string name;
  bool side = false;
  bool top = false;
};
inline std::vector<space_item_shown>& space_items_now() {
  static std::vector<space_item_shown> items;
  return items;
}
inline std::string& space_account_now() {
  static std::string account;
  return account;
}
// What each level holds of the looks, as the program last said: none, as
// the level over it. For the choices to show what is chosen where.
struct looks_held {
  std::optional<config::wallpaper_t> wallpaper;
  std::optional<config::bubble_look> bubbles;
  std::optional<config::bubble_look> panels;
};
inline looks_held& looks_at(const choice_level_t& level) {
  static looks_held everywhere, account, chat;
  return splice::visit(splice::overloaded{[](choice_level::everywhere) -> looks_held& { return everywhere; },
                                          [](choice_level::account) -> looks_held& { return account; },
                                          [](choice_level::chat) -> looks_held& { return chat; }},
                       level);
}
// And the panels': every chat's, and the chosen chat's.
inline config::bubble_look& panel_look_everywhere() {
  static config::bubble_look look;
  return look;
}
inline config::bubble_look& panel_look_now() {
  static config::bubble_look look;
  return look;
}
// The panels' opacity going from one chat's to another's: eased, the look
// otherwise as it is.
struct panel_ease_t {
  skiff::paint::Tween t{1.0f, 260.0f};
  float from = 1.0f, to = 1.0f;
};
inline panel_ease_t& panel_ease() {
  static panel_ease_t ease;
  return ease;
}
// The panels' look put in place, for skiff to paint them in: only over the
// background behind the whole window. Whether it changed -- the window to
// be repainted.
inline bool show_panels(const config::bubble_look& look) {
  const bool kinded = window_look().behind && splice::visit(splice::overloaded{[](config::bubbles::solid) { return false; },
                                                                              [](const auto&) { return true; }},
                                                           look.kind);
  scene::detail::PanelLook next{
      .active = kinded,
      .opacity = static_cast<float>(look.opacity) / 100.0f,
      .frosted = splice::visit(splice::overloaded{[](config::bubbles::frosted) { return true; }, [](const auto&) { return false; }}, look.kind),
      .edge = splice::visit(splice::overloaded{[](config::bubbles::glass) { return true; }, [](const auto&) { return false; }}, look.kind),
      .panels = {sidebar_colour},
      .tints = {chosen_colour}};
  if (!kinded)
    next = {};
  if (next == scene::detail::panelLook())
    return false;
  // Only the opacity another: eased to it, from where it is now.
  scene::detail::PanelLook& now = scene::detail::panelLook();
  const bool same_but_opacity = now.active && next.active && now.frosted == next.frosted && now.edge == next.edge &&
                                now.panels == next.panels && now.tints == next.tints;
  if (same_but_opacity) {
    auto& ease = panel_ease();
    if (ease.t.moving() && ease.to == next.opacity)
      return false;
    ease.from = now.opacity;
    ease.to = next.opacity;
    ease.t.jump(0.0f);
    ease.t.setTarget(1.0f);
    return true;
  }
  panel_ease().t.jump(1.0f);
  now = std::move(next);
  return true;
}
// A chat background's dialog, for a level.
template <class Actions>
struct open_wallpaper_at {
  Actions* actions = nullptr;
  choice_level_t level;
  void operator()() const { actions->open_wallpaper(level); }
};
// A background's picture, read from where mux keeps it and decoded once.
inline skia::Sp<skia::SkImage> wallpaper_picture(const std::string& path) {
  static std::map<std::string, skia::Sp<skia::SkImage>> read;
  if (const auto found = read.find(path); found != read.end())
    return found->second;
  std::ifstream in(path, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  auto image = bytes.empty() ? skia::Sp<skia::SkImage>() : skia::decodeImage(bytes.data(), bytes.size());
  read.insert_or_assign(path, image);
  return image;
}
// Pictures of what a dialog lists that no chat holds -- Explore's rooms,
// Start chat's people found -- by their key (a room's or a person's id) and
// their mxc://: fetched as avatars are, while the dialog shows them.
inline std::vector<std::pair<std::string, std::string>>& listed_avatars() {
  static std::vector<std::pair<std::string, std::string>> listed;
  return listed;
}
// The images of the pack being edited: fetched as avatars are, keyed by
// their mxc://, while its dialog shows them.
inline std::vector<std::string>& pack_pictures_shown() {
  static std::vector<std::string> shown;
  return shown;
}
// And Telegram's pattern over it: dark and faint on a light theme, light and
// fainter on a dark one.
inline skia::SkColor pattern_colour = skia::colorSetARGB(20, 255, 255, 255);
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
//   void toggle_emoji(), close_emoji(), insert_emoji(std::string text, std::string picture)  -- the input's emoji panel
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
