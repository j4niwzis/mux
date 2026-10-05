// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:base -- The palette, the protocol of an address, the program asked, and laying out.
export module mux.ui:base;

import std;
import splice.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.widgets.theme;
import skiff.widgets.wallpaper;
import skiff.widgets.motion;
import mux.core;
import mux.protocols;
import mux.config;
import mux.logic.text;
import mux.platform.audio;

export namespace mux::ui {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;
namespace widgets = skiff::widgets;

// The theme's colours: made by palette_of (themes.cc) from the theme and
// the accent chosen, the program's, and handed down to what is drawn in
// them. As it is here: the dark one the window starts in.
struct palette {
  skia::SkColor background = skia::colorSetARGB(255, 24, 27, 30);
  skia::SkColor sidebar = skia::colorSetARGB(255, 32, 36, 40);
  skia::SkColor chosen = skia::colorSetARGB(255, 52, 60, 66);
  skia::SkColor text = skia::colorSetARGB(255, 235, 240, 243);
  skia::SkColor dim = skia::colorSetARGB(255, 150, 162, 170);
  skia::SkColor accent = skia::colorSetARGB(255, 102, 204, 255);
  skia::SkColor error = skia::colorSetARGB(255, 255, 120, 110);
  skia::SkColor selected = skia::colorSetARGB(255, 43, 82, 120);
  skia::SkColor selected_text = skia::colorSetARGB(255, 255, 255, 255);
  skia::SkColor band = skia::colorSetARGB(255, 18, 20, 23);
  skia::SkColor section = skia::colorSetARGB(255, 26, 29, 33);
  skia::SkColor tile = skia::colorSetARGB(255, 40, 45, 50);
  skia::SkColor bubble = skia::colorSetARGB(255, 33, 41, 52);
  skia::SkColor out_bubble = skia::colorSetARGB(255, 43, 82, 120);
  skia::SkColor sent_time = skia::colorSetARGB(255, 170, 200, 230);
  skia::SkColor chat = skia::colorSetARGB(255, 14, 22, 33);
  skia::SkColor chat_top = skia::colorSetARGB(255, 22, 38, 58);
  skia::SkColor pattern = skia::colorSetARGB(20, 255, 255, 255);
  skia::SkColor on_accent = skia::colorSetARGB(255, 255, 255, 255);
  // skiff-widgets' colours for the theme, with its accent: handed to each
  // control made.
  skiff::widgets::Theme widgets{};
  // A menu's plate: the side's colour, all but opaque -- never taken for a
  // panel's fill, so a see-through panel look leaves menus readable over it.
  [[nodiscard]] skia::SkColor popup() const { return (sidebar & 0x00FFFFFFu) | (0xFEu << 24); }
  friend bool operator==(const palette&, const palette&) = default;
};
// An element's opacity in a look, in percent: its own, else the bubbles'
// -- all of it where they are solid.
[[nodiscard]] inline int element_opacity_of(const config::bubble_look& look, std::optional<int> config::element_opacity::* which) {
  if (const auto& own = look.elements.*which)
    return *own;
  return splice::visit(splice::overloaded{[](config::bubbles::solid) { return 100; }, [&](const auto&) { return look.opacity; }},
                       look.kind);
}
// How much a look's Frosted blurs, 0 to 1: its own, else the window's.
struct window_look_t;
[[nodiscard]] inline float blur_of(const config::bubble_look& look, const window_look_t& window);
// An element's, drawn frosted: its own, else its look's.
[[nodiscard]] inline float element_blur_of(const config::bubble_look& look, std::optional<double> config::element_blur::* which,
                                           const window_look_t& window);
// Whether a look frosts.
[[nodiscard]] inline bool frosts(const config::bubble_look& look) {
  return splice::visit(splice::overloaded{[](config::bubbles::frosted) { return true; }, [](const auto&) { return false; }}, look.kind);
}
// A menu's plate: the side's colour, all but opaque -- never taken for a
// panel's fill, so a see-through panel look leaves menus readable over it.
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
  double frost = 10.0;       // how much Frosted blurs, in percent of the most
  bool spaces = true;        // the space bars at all
  bool top_bar = true;       // the one along the top
  bool home_hides = false;   // Home without what spaces hold, for the client
  bool home_direct = false;  // and without direct messages
  bool live_blur = false;   // frosted popups and sheets blur what is under them, live
  int interface_scale = 100;  // in percent of the display's scale
};
// The interface scales one can choose, in percent: Telegram Desktop's from
// 100 up, and smaller ones under it -- a window on a small screen, or one
// that wants more in it (the user, #13738).
inline constexpr std::array kScales{50,  60,  70,  75,  80,  90,  100, 110, 120, 125, 130, 140,
                                    150, 160, 170, 175, 180, 190, 200, 225, 250, 275, 300};
inline constexpr int kScaleLeast = kScales.front();
inline constexpr int kScaleMost = kScales.back();
// The items of the space bars of the account shown, and where each is: for
// the settings to list them. Said by the chat list as it shows them.
struct space_item_shown {
  config::space_item_t item;
  std::string name;
  bool side = false;
  bool top = false;
};
// What the window's parts tell one another and the program, as the program
// holds it: handed down with the colours (ui_needs).
struct ui_shared {
  // The emoji, stickers and GIFs docked at the bottom of a phone's window,
  // as Telegram's apps have them -- where the keyboard would be: how high it
  // is, for the chat to stand its field over it; and the screen that does,
  // told as it changes. None while it is not up.
  float docked_panel_height = 0.0f;
  scene::NodeId docked_panel_watcher = 0;
  void set_docked_panel_height(float high) {
    if (high == docked_panel_height)
      return;
    docked_panel_height = high;
    if (docked_panel_watcher != 0)
      scene::work::mark(docked_panel_watcher);
  }
  // The items of the space bars of the account shown, and where each is,
  // and that account: for the settings to list them. Said by the chat list
  // as it shows them.
  std::vector<space_item_shown> space_items;
  std::string space_account;
  // The images of the pack being edited, and the stickers the emoji panel
  // shows -- in view, and its packs' tabs: fetched as avatars are, keyed by
  // their mxc://, while they show.
  std::vector<std::string> pack_pictures_shown;
  std::vector<std::string> panel_pictures_shown;
  // A viewport moved onto messages whose media has not been requested yet.
  bool pictures_due = false;
  // What each protocol's account does beyond what every one does, as the
  // program found it from the account types as it started; and each
  // account's protocol state, as its client last said it.
  std::map<protocol_t, proto::account_ops> protocol_ops;
  std::map<account_id, protocol_state_t> protocol_states;
};
// A message found by a chat's search, as the list of them shows it: its
// place among them, who said it, when, and its words around what was found.
struct search_result {
  std::size_t index = 0;
  std::string sender;
  std::string name;
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  std::string snippet;
};
// What each level holds of the looks, as the program last said: none, as
// the level over it. For the choices to show what is chosen where.
struct looks_held {
  std::optional<config::wallpaper_t> wallpaper;
  std::optional<config::bubble_look> bubbles;
  std::optional<config::bubble_look> panels;
};
// The looks the window shows, as the program holds them: handed down with
// the colours (ui_needs).
struct looks_shown {
  // The window's own: its opacity, the background behind it, Frosted's blur.
  window_look_t window;
  // How the bubbles of the chat shown look -- set before its bubbles are
  // made -- and the panels; and every chat's of each.
  config::bubble_look bubbles, panels;
  config::bubble_look bubbles_everywhere, panels_everywhere;
  // What each level holds.
  looks_held everywhere, account, chat;
  template <class Self>
  [[nodiscard]] auto& at(this Self& self, const choice_level_t& level) {
    return splice::visit(splice::overloaded{[&](choice_level::everywhere) -> auto& { return self.everywhere; },
                                            [&](choice_level::account) -> auto& { return self.account; },
                                            [&](choice_level::chat) -> auto& { return self.chat; }},
                         level);
  }
};
// The panels' opacity going from one chat's to another's: eased, the look
// otherwise as it is.
struct panel_ease_t {
  skiff::paint::Tween t{1.0f, 260.0f};
  float from = 1.0f, to = 1.0f;
};
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
  std::string bytes = splice::bytes::file_text(path).value_or(std::string());
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
// What each protocol's account does beyond what every one does, as the
// program found it from the account types (ui_shared::protocol_ops); and an
// account's, by its protocol. None found: nothing beyond.
[[nodiscard]] inline proto::account_ops ops_of(const ui_shared& shared, const account_id& of) {
  const auto found = shared.protocol_ops.find(of.speaks);
  return found != shared.protocol_ops.end() ? found->second : proto::account_ops{};
}
// Whether files may be sent into an account's chats: where its account
// sends them, and its protocol allows it now.
[[nodiscard]] inline bool may_send_files(const ui_shared& shared, const account_id& of);
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
  using type = splice::variant<Ts...>;
};
// A node a press acts on -- a row, a tile, a tab: it takes the pointer, is
// lit under it, and a click calls its act.
template <class Base>
struct pressable : Base {
  using Base::Base;
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(this auto& self, float, float) {
    self.act();
    return true;
  }
};
// The emoji and stickers kept (emoji_kept), and how fills are painted
// (mux_paint).
struct emoji_kept;
struct mux_paint;
// What the window's nodes are handed down, from the root -- the program's
// own objects, each a pointer of a type of its own: what a node reads, it is
// given by its parent, and takes what it needs of it with splice::remapped<>.
template <class Actions>
struct ui_needs {
  Actions* actions = nullptr;
  // What plays voice messages: the program's.
  platform::audio::speaker* sound = nullptr;
  // The theme's colours: the program's.
  const palette* colours = nullptr;
  // The emoji and stickers kept: the program's.
  emoji_kept* emoji = nullptr;
  // The looks shown, and how fills are painted: the program's.
  looks_shown* looks = nullptr;
  mux_paint* paint = nullptr;
  // What the window's parts tell one another: the program's.
  ui_shared* shared = nullptr;
};
// A dialog as what it shows wants it: which of the palette's colours its
// sheet is; its size -- fixed, as wide as fits what it shows up to a
// width, or said as it is opened; where it sits; whether a click outside
// closes it. Each dialog's content declares its own (look_of_dialog), and
// the window's layers are walked to put them in place.
namespace sheet {
struct side {};
struct chat {};
}  // namespace sheet
namespace dialog_size {
struct fixed {
  float width, height;
};
struct fitting {
  float width;
};
struct as_opened {};
}  // namespace dialog_size
struct dialog_look {
  splice::variant<sheet::side, sheet::chat> sheet = sheet::side{};
  splice::variant<dialog_size::as_opened, dialog_size::fixed, dialog_size::fitting> size = dialog_size::as_opened{};
  widgets::DialogPlace place = widgets::dialog_place::centred{};
  bool dismissable = true;
};
[[nodiscard]] inline skia::SkColor colour_of(sheet::side, const palette& colours) { return colours.sidebar; }
[[nodiscard]] inline skia::SkColor colour_of(sheet::chat, const palette& colours) { return colours.chat; }
// A dialog put as its content says, in the palette's colours.
template <class Content>
void look_as_its_content(widgets::Dialog<Content>& dialog, const palette& colours) {
  const dialog_look look = Content::look_of_dialog();
  dialog.setSheetColour(splice::visit([&](auto one) { return colour_of(one, colours); }, look.sheet));
  splice::visit(splice::overloaded{[](dialog_size::as_opened) {},
                                   [&](dialog_size::fixed size) { dialog.setSize(size.width, size.height); },
                                   [&](dialog_size::fitting size) { dialog.setWidthFittingContent(size.width); }},
                look.size);
  dialog.setPlace(look.place);
  dialog.setDismissable(look.dismissable);
}
// Anything else a window holds: no dialog, nothing to put.
inline void look_as_its_content(const auto&, const palette&) {}

// Each account's protocol state, as its client last said it
// (ui_shared::protocol_states): what the extension points are asked with.
// One not said yet: its protocol's default.
[[nodiscard]] inline protocol_state_t protocol_state_of(const ui_shared& shared, const account_id& of) {
  const auto found = shared.protocol_states.find(of);
  return found != shared.protocol_states.end() ? found->second : state_before(of.speaks);
}
[[nodiscard]] inline bool may_send_files(const ui_shared& shared, const account_id& of) {
  return ops_of(shared, of).send_file && proto::can_upload(protocol_state_of(shared, of));
}
// And Telegram's pattern over it: dark and faint on a light theme, light and
// fainter on a dark one.

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
//   void add_account_of(protocol_t)  -- a protocol's form, when adding
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
//   void account_page(account_page_t) -- a page of the chosen account
//   void flip_account_receipts(), flip_only_verified(), accept_identity(who), flip_account_typing(), choose_account_proxy(int), manage_proxies()
//   template <class Request> void ask_for(Request) -- a protocol's own request (asks<Actions, Request>)
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
//        search_elsewhere(query)  -- the chat list's search, where nothing joined matches,
//        create_room(name, topic, open, alias, federate, encrypted)  -- rooms found and made
//   void settings_notifications(), flip_notify(notify_flag_t), set_notify_backend(notify_backend_t),
//        set_notify_choice(choice_level_t, notify_setting_t, optional<bool>)  -- notifications, at a level
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
// A protocol's own request with nothing to say but itself, as its UI asks
// it: `asks<Actions, request::refresh_sessions>`, through the program's
// ask_for.
template <class Actions, class Request>
struct asks {
  Actions* actions = nullptr;
  void operator()() const { actions->ask_for(Request{}); }
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


// A look's blur, 0 to 1: its own, else the window's Frosted blur.
[[nodiscard]] inline float blur_of(const config::bubble_look& look, const window_look_t& window) {
  return static_cast<float>(look.blur.value_or(window.frost) / 100.0);
}
[[nodiscard]] inline float element_blur_of(const config::bubble_look& look, std::optional<double> config::element_blur::* which,
                                           const window_look_t& window) {
  if (const auto& own = look.blurs.*which)
    return static_cast<float>(*own / 100.0);
  return blur_of(look, window);
}

// ---- what is painted behind ---------------------------------------------------
// The backdrops the wallpapers offered as they were drawn, by wallpaper, in
// the order they lie -- the window's own under a chat's: what frosted things
// draw a piece of, those under them. Each wallpaper is a wallpaper_t,
// offering it here; one gone is let go of as another offers.
inline std::vector<std::pair<scene::NodeId, widgets::Backdrop>>& frost_backdrops() {
  static std::vector<std::pair<scene::NodeId, widgets::Backdrop>> kept;
  return kept;
}
struct frost_out {
  static void offer(scene::NodeId id, const widgets::Backdrop& one) {
    auto& all = frost_backdrops();
    std::erase_if(all, [](const auto& each) { return scene::work::entry(each.first) == nullptr; });
    if (const auto at = std::ranges::find(all, id, &std::pair<scene::NodeId, widgets::Backdrop>::first); at != all.end())
      at->second = one;
    else
      all.emplace_back(id, one);
  }
};
using wallpaper_t = widgets::Wallpaper<frost_out>;
struct frost_source {
  [[nodiscard]] auto operator()() const { return frost_backdrops() | std::views::values; }
};
// Frosted glass behind what a node holds: its first part, filling it.
using frost_pane = widgets::BackdropPane<frost_source>;

// Panels -- a window's columns and bars -- over what is behind the whole
// window: their fill at an opacity, frosted, or with a light edge, one look
// for all of them, read as they are painted: set again (a chat opened may
// change it) and the window repainted, nothing made again. A panel's fill is
// known by its colour, one of `panels`; a fill of the same colour inside a
// panel's is the same fill again, not drawn -- two at an opacity were
// darker. `tints` -- a row hovered, the one chosen -- only at the opacity.
struct panel_look_t {
  bool active = false;
  float opacity = 1.0f;
  bool frosted = false;
  float blur = -1.0f;  // how much the frost blurs, 0 to 1; below 0, the backdrop's own
  bool edge = false;
  std::vector<skia::SkColor> panels;
  std::vector<skia::SkColor> tints;
  friend bool operator==(const panel_look_t&, const panel_look_t&) = default;
};
// A float's backdrop kept while it moves -- sliding in, fading -- as the
// user asked (#13420): what is under the window taken once, as it comes up,
// blurred once, small, and drawn from until it stands still; then blurred
// live again, what is under it being what changes. Not blurred anew at each
// frame of its animation, all of what it covers at full size.
struct kept_blur {
  skia::SkRect at = skia::SkRect::MakeEmpty();  // where it was last drawn, on the device
  float alpha = -1.0f;                          // and at what alpha
  bool moved = false;                           // whether it had moved then
  skia::Sp<skia::SkImage> blurred;              // the window under it, blurred, small
  skia::SkRect device = skia::SkRect::MakeEmpty();  // where that is on the device: all of the window
};
// What is drawn on a canvas's pixels so far, blurred by `sigma` (in its
// pixels): taken a quarter of the size and blurred there -- a sixteenth of
// the pixels. None where the canvas has no pixels of its own (a recording).
[[nodiscard]] inline skia::Sp<skia::SkImage> blurred_window(skia::SkCanvas* canvas, float sigma, skia::SkRect& device) {
  skia::SkSurface* surface = canvas->getSurface();
  if (!surface)
    return nullptr;
  constexpr float shrink = 4.0f;
  skia::SkBitmap small;
  {
    // Let go of before anything more is drawn: kept, it would make the
    // window's pixels be copied at the next draw.
    const skia::Sp<skia::SkImage> under = surface->makeImageSnapshot();
    if (!under)
      return nullptr;
    const int width = std::max(1, static_cast<int>(std::ceil(static_cast<float>(under->width()) / shrink)));
    const int height = std::max(1, static_cast<int>(std::ceil(static_cast<float>(under->height()) / shrink)));
    if (!small.tryAllocN32Pixels(width, height))
      return nullptr;
    small.eraseColor(0);
    skia::SkCanvas into(small);
    skia::SkPaint paint;
    paint.setImageFilter(skia::SkImageFilters::Blur(sigma / shrink, sigma / shrink, skia::SkTileMode::kClamp, nullptr));
    into.drawImageRect(under, skia::SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
                       skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    device = skia::SkRect::MakeIWH(under->width(), under->height());
  }
  return small.asImage();
}
// What is under a node, blurred as it is drawn: a backdrop filter in its
// shape -- `blur` 0 to 1 as Frosted's, the window's where below 0 -- and
// its rect noted, for the host to repaint all of it with what is under it.
// While it moves, the window under it as it came up, blurred once (above).
inline void live_backdrop(std::vector<std::pair<scene::NodeId, kept_blur>>& all, const scene::State& state, skia::SkCanvas* canvas,
                          float blur, double frost, float alpha) {
  const skia::SkRect on = canvas->getTotalMatrix().mapRect(state.fBounds);
  // Shown again after it was let go of -- a dialog's sheet, shut and opened
  // again: come up now, as a new one.
  const bool shown_before = scene::detail::liveBackdrops().contains(state.fId);
  scene::detail::LiveBackdrop& noted = scene::detail::liveBackdrops()[state.fId];
  noted = {on, scene::work::frameNumber(), false};
  // Into a recording, played back in bands: nothing under it to read.
  const bool recording = canvas->getSurface() == nullptr;
  const float amount = blur >= 0.0f ? blur : static_cast<float>(frost / 100.0);
  const float sigma = 1.0f + amount * 30.0f;
  const skia::SkRRect shape = scene::detail::roundedBox(state, state.fBounds);
  // A Gaussian's reach: three sigmas, on the device.
  noted.reach = std::ceil(3.0f * sigma * std::max(1.0f, canvas->getTotalMatrix().getScaleX())) + 1.0f;

  std::erase_if(all, [](const auto& each) { return scene::work::entry(each.first) == nullptr; });
  auto found = std::ranges::find(all, state.fId, &std::pair<scene::NodeId, kept_blur>::first);
  // Come up now: what is under it is all that is drawn yet -- what is not
  // repainted is the frame before, without it.
  const bool new_one = found == all.end() || !shown_before;
  if (found == all.end()) {
    all.emplace_back(state.fId, kept_blur{});
    found = std::prev(all.end());
  }
  if (new_one)
    found->second = kept_blur{};
  kept_blur& kept = found->second;
  const bool moved = new_one || kept.at != on || kept.alpha != alpha;
  // Set off again from standing still, where all of the window is being
  // repainted: what is under it all drawn anew, it not yet -- taken again.
  const skia::SkIRect clip = canvas->getDeviceClipBounds();
  const bool whole = canvas->getSurface() && clip.width() >= canvas->getSurface()->width() &&
                     clip.height() >= canvas->getSurface()->height();
  if (new_one || (moved && (!kept.moved || !kept.blurred) && whole))
    kept.blurred = blurred_window(canvas, sigma, kept.device);
  kept.at = on;
  kept.alpha = alpha;
  kept.moved = moved;

  // Moving, the frame may go in bands: what it keeps reads nothing under it.
  noted.kept = moved && kept.blurred;
  // Still, in a recording: drawn from what it keeps for this frame, and all
  // of the window repainted at the next, not in bands, blurred live. With
  // nothing kept, blurred live here, cut at the bands' edges, and the same.
  if (recording && !moved)
    scene::detail::liveBackdropsStale() = true;
  if (recording && !kept.blurred)
    scene::detail::liveBackdropsStale() = true;
  skia::SkMatrix inverse;
  if ((moved || recording) && kept.blurred && canvas->getTotalMatrix().invert(&inverse)) {
    const skia::SkMatrix local = skia::SkMatrix::RectToRect(
        skia::SkRect::MakeIWH(kept.blurred->width(), kept.blurred->height()), inverse.mapRect(kept.device));
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    paint.setShader(kept.blurred->makeShader(skia::SkTileMode::kClamp, skia::SkTileMode::kClamp,
                                             skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &local));
    canvas->drawRRect(shape, paint);
    return;
  }
  const auto filter = skia::SkImageFilters::Blur(sigma, sigma, nullptr);
  skia::SkPaint paint;
  paint.setAlphaf(alpha);
  const int saved = canvas->save();
  canvas->clipRRect(shape, true);
  canvas->saveLayer(skia::SkCanvas::SaveLayerRec(&state.fBounds, &paint, filter.get(), 0));
  canvas->restoreToCount(saved);
}
// How mux paints every box's fill, as skiff asks a program (ProgramPaint):
// the panels' look.
struct mux_paint : scene::Painting {
  // The looks it paints by: the program's.
  const looks_shown* looks = nullptr;
  // And the theme's colours: a popup's plate told from a panel's.
  const palette* colours = nullptr;
  // The panels' look, as the program put it (show_panels), and its opacity
  // eased from one chat's to another's.
  panel_look_t panel;
  panel_ease_t ease;
  // Being drawn inside a panel's fill: set by the fill, put back as the node
  // that has it is done; and the node whose fill was a panel's, for its
  // edge. And inside what floats over others -- a layer sliding in, a
  // dialog -- set by it, put back as it is done.
  bool inside_panel = false;
  bool inside_float = false;
  scene::NodeId panel_painted = 0;
  // The blurs kept of what is under each float, while it moves.
  std::vector<std::pair<scene::NodeId, kept_blur>> blurs;

  std::optional<skia::SkColor> under(const scene::State& state, std::optional<skia::SkColor> fill, skia::SkCanvas* canvas,
                                            float alpha) {
    const panel_look_t& look = panel;
    if (!fill || !look.active)
      return fill;
    // A panel's fill on what floats, live: its sheet under it shows what is
    // behind already -- frosted again from the wallpaper, a title bar showed
    // the picture over the messages the sheet blurred.
    if (inside_float && !state.fFloats && look.frosted && looks->window.live_blur &&
        std::ranges::contains(look.panels, *fill))
      return std::nullopt;
    // Floating over others -- a popup, a sheet -- frosted, and asked so:
    // what is really under it blurred, as it is drawn, its fill over that.
    if (state.fFloats && look.frosted && looks->window.live_blur) {
      const bool panel = std::ranges::contains(look.panels, *fill);
      if (panel && inside_panel)
        return std::nullopt;
      live_backdrop(blurs, state, canvas, look.blur, looks->window.frost, alpha);
      if (panel) {
        inside_panel = true;
        panel_painted = state.fId;
      }
      return scene::detail::atOpacity(*fill, look.opacity);
    }
    if (std::ranges::contains(look.panels, *fill)) {
      if (inside_panel)
        return std::nullopt;
      inside_panel = true;
      panel_painted = state.fId;
      if (look.frosted)
        widgets::drawBackdrops(canvas, frost_source{}(), look.blur, scene::detail::roundedBox(state, state.fBounds), alpha);
      return scene::detail::atOpacity(*fill, look.opacity);
    }
    // A popup's plate on another floating one -- a submenu over its menu,
    // Seen's list over the message's menu: as it is, near opaque. At the
    // panels' opacity it let the plate under it through where they met --
    // darker there, lighter where it stood out of it.
    if (*fill == colours->popup() && inside_float && !state.fFloats)
      return fill;
    if (std::ranges::contains(look.tints, *fill))
      return scene::detail::atOpacity(*fill, look.opacity);
    return fill;
  }
  // Glass: a light edge round the panel.
  void over(const scene::State& state, skia::SkCanvas* canvas, float alpha) {
    if (!panel.edge || panel_painted != state.fId)
      return;
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(skia::kStrokeStyle);
    paint.setStrokeWidth(1.0f);
    paint.setColor(scene::detail::atOpacity(0xFFFFFFFFu, 0.22f));
    paint.setAlphaf(paint.getAlphaf() * alpha);
    canvas->drawRRect(scene::detail::roundedBox(state, state.fBounds, 0.5f), paint);
  }
  // A panel's fill found in a node holds for what is under it, and no further.
  struct scope {
    mux_paint* paint;
    bool was;
    bool was_float;
    scope(mux_paint& p, const scene::State& state) : paint(&p), was(p.inside_panel), was_float(p.inside_float) {
      if (state.fFloats)
        p.inside_float = true;
    }
    ~scope() {
      paint->inside_panel = was;
      paint->inside_float = was_float;
    }
  };
};

// A message a chat holds, wherever: its timeline, an answer in one of its
// threads, or fetched aside for a quote. What a reply quotes, a menu acts
// on, a reaction goes to -- an answer in a thread among them, which the
// timeline alone does not have.
[[nodiscard]] inline const message* held_message(const conversation& chat, std::string_view id) {
  if (const auto it = std::ranges::find(chat.timeline, id, &message::id); it != chat.timeline.end())
    return &*it;
  for (const auto& [root, answers] : chat.threads)
    if (const auto it = std::ranges::find(answers, id, &message::id); it != answers.end())
      return &*it;
  if (const auto aside = chat.quoted.find(std::string(id)); aside != chat.quoted.end())
    return &aside->second;
  return nullptr;
}
}  // namespace mux::ui

// mux's way of painting fills, for skiff to find wherever mux's tree is drawn.
template <>
struct skiff::scene::detail::ProgramPaint<void> {
  using type = mux::ui::mux_paint;
};
