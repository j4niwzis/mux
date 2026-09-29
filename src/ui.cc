// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui: the window's screens, as skiff nodes -- the conversations of every
// account down the side with the chosen one beside them, under a top bar
// that swaps in the panels for adding an account and for the accounts, all in
// the one window.
//
// The screens are views. What a control does is ask the program, through
// `Actions`, and the program changes the screens between events: a screen
// switched or a list rebuilt inside a click would destroy the control whose
// handler is still running.
export module mux.ui;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;

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
  return config::is_matrix(address) ? protocol_t{protocol::matrix{}} : protocol_t{protocol::xmpp{}};
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
//   void message_menu(menu_facts), menu_copy_link(), menu_save()  -- a message's menu
//   void reply_to(std::string id, std::string text)  -- a message swiped left
//   void open_picture(std::string source, std::string sender, std::string name, std::string when)
//   void close_picture(), save_picture(std::string source), open_file(std::string source, std::string name)
//   void attach_files(), close_send_box(), send_files()  -- what is sent with the paperclip
//   void open_member_info(std::string id)  -- a sender's page, in the info
//   void load_older(const conversation_id&, std::string from)  -- its history
//   void jump_to_end()               -- back to a chat's newest message
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

// ---- icons and rows ---------------------------------------------------------------

// The icons, drawn with a pen rather than taken from a font: each its own
// type, drawn by its own overload.
namespace icon {
struct none {};
struct person {};
struct gear {};
struct power {};
struct plus {};
struct motion {};
struct back {};
struct close {};
struct info {};
struct people {};
struct add_person {};
struct bell {};
struct sliders {};
struct leave {};
struct check {};
struct clip {};
struct send {};
struct eye {};
struct minus {};
// A filled dot of a colour of its own, as a proxy profile's.
struct dot {
  skia::SkColor colour;
};
}  // namespace icon
using icon_t = std::variant<icon::none, icon::person, icon::gear, icon::power, icon::plus, icon::motion, icon::back,
                            icon::close, icon::info, icon::people, icon::add_person, icon::bell, icon::sliders,
                            icon::leave, icon::check, icon::clip, icon::send, icon::eye, icon::dot, icon::minus>;

[[nodiscard]] inline skia::SkPaint pen(skia::SkColor colour, float alpha, float width = 1.8f) {
  skia::SkPaint out;
  out.setAntiAlias(true);
  out.setColor(colour);
  out.setAlphaf(alpha);
  out.setStyle(skia::kStrokeStyle);
  out.setStrokeWidth(width);
  out.setStrokeCap(skia::kRoundCap);
  return out;
}

// Each in a box, centred in it, about 20 points across.
inline void draw_icon(skia::SkCanvas*, icon::none, const skia::SkRect&, skia::SkColor, float) {}
inline void draw_icon(skia::SkCanvas* canvas, icon::person, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x, y - 4.0f, 3.8f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 7.5f, y + 2.0f, x + 7.5f, y + 17.0f), 180.0f, 180.0f, false, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::gear, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x, y, 2.8f, p);
  canvas->drawCircle(x, y, 6.5f, p);
  for (int k = 0; k < 8; ++k) {
    const float a = static_cast<float>(k) * std::numbers::pi_v<float> / 4.0f;
    canvas->drawLine(x + 6.5f * std::cos(a), y + 6.5f * std::sin(a), x + 9.0f * std::cos(a), y + 9.0f * std::sin(a),
                     pen(colour, alpha, 2.4f));
  }
}
inline void draw_icon(skia::SkCanvas* canvas, icon::power, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY() + 1.0f;
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 7.5f, y - 7.5f, x + 7.5f, y + 7.5f), 300.0f, 300.0f, false, p);
  canvas->drawLine(x, y - 9.5f, x, y - 2.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::plus, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 7.0f, y, x + 7.0f, y, p);
  canvas->drawLine(x, y - 7.0f, x, y + 7.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::motion, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x + 3.5f, y, 4.5f, p);
  canvas->drawLine(x - 9.0f, y - 3.5f, x - 3.5f, y - 3.5f, p);
  canvas->drawLine(x - 7.0f, y + 3.5f, x - 3.5f, y + 3.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::back, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x + 7.0f, y, x - 7.0f, y, p);
  canvas->drawLine(x - 7.0f, y, x - 1.5f, y - 5.5f, p);
  canvas->drawLine(x - 7.0f, y, x - 1.5f, y + 5.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::close, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.0f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 6.0f, y - 6.0f, x + 6.0f, y + 6.0f, p);
  canvas->drawLine(x - 6.0f, y + 6.0f, x + 6.0f, y - 6.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::info, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawCircle(x, y, 8.5f, p);
  canvas->drawLine(x, y - 1.0f, x, y + 4.5f, pen(colour, alpha, 2.0f));
  canvas->drawCircle(x, y - 4.5f, 0.6f, pen(colour, alpha, 2.2f));
}
inline void draw_icon(skia::SkCanvas* canvas, icon::people, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.6f);
  const float x = box.centerX() - 2.5f, y = box.centerY();
  canvas->drawCircle(x, y - 4.0f, 3.2f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 6.5f, y + 2.0f, x + 6.5f, y + 14.0f), 180.0f, 180.0f, false, p);
  canvas->drawCircle(x + 7.0f, y - 3.0f, 2.6f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x + 3.5f, y + 2.5f, x + 12.0f, y + 12.0f), 200.0f, 140.0f, false, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::add_person, const skia::SkRect& box, skia::SkColor colour,
                      float alpha) {
  const auto p = pen(colour, alpha, 1.6f);
  const float x = box.centerX() - 3.0f, y = box.centerY();
  canvas->drawCircle(x, y - 4.0f, 3.4f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 7.0f, y + 2.0f, x + 7.0f, y + 15.0f), 180.0f, 180.0f, false, p);
  canvas->drawLine(x + 9.0f, y - 6.0f, x + 9.0f, y + 0.0f, p);
  canvas->drawLine(x + 6.0f, y - 3.0f, x + 12.0f, y - 3.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::bell, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 6.0f, y - 8.0f, x + 6.0f, y + 4.0f), 180.0f, 180.0f, false, p);
  canvas->drawLine(x - 6.0f, y - 2.0f, x - 7.5f, y + 4.0f, p);
  canvas->drawLine(x + 6.0f, y - 2.0f, x + 7.5f, y + 4.0f, p);
  canvas->drawLine(x - 7.5f, y + 4.0f, x + 7.5f, y + 4.0f, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 2.5f, y + 4.5f, x + 2.5f, y + 9.0f), 0.0f, 180.0f, false, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::sliders, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 8.0f, y - 4.0f, x + 8.0f, y - 4.0f, p);
  canvas->drawLine(x - 8.0f, y + 4.0f, x + 8.0f, y + 4.0f, p);
  canvas->drawCircle(x - 3.0f, y - 4.0f, 2.4f, p);
  canvas->drawCircle(x + 3.5f, y + 4.0f, 2.4f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::leave, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 2.0f, y - 8.0f, x - 8.0f, y - 8.0f, p);
  canvas->drawLine(x - 8.0f, y - 8.0f, x - 8.0f, y + 8.0f, p);
  canvas->drawLine(x - 8.0f, y + 8.0f, x - 2.0f, y + 8.0f, p);
  canvas->drawLine(x - 3.0f, y, x + 8.0f, y, p);
  canvas->drawLine(x + 8.0f, y, x + 4.5f, y - 3.5f, p);
  canvas->drawLine(x + 8.0f, y, x + 4.5f, y + 3.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::check, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 2.2f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 5.0f, y, x - 1.5f, y + 4.0f, p);
  canvas->drawLine(x - 1.5f, y + 4.0f, x + 6.0f, y - 4.5f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::clip, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  const int save = canvas->save();
  canvas->translate(x, y);
  canvas->rotate(45.0f);
  canvas->drawRoundRect(skia::SkRect::MakeLTRB(-3.5f, -10.0f, 3.5f, 8.0f), 3.5f, 3.5f, p);
  canvas->drawLine(0.0f, -5.5f, 0.0f, 4.0f, p);
  canvas->restoreToCount(save);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::send, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  // Telegram's: a paper plane, filled, pointing right.
  skia::SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(colour);
  fill.setAlphaf(alpha);
  const float x = box.centerX(), y = box.centerY();
  skia::SkPathBuilder plane;
  plane.moveTo(x - 9.0f, y - 8.5f);
  plane.lineTo(x + 10.0f, y);
  plane.lineTo(x - 9.0f, y + 8.5f);
  plane.lineTo(x - 7.0f, y + 1.5f);
  plane.lineTo(x + 2.0f, y);
  plane.lineTo(x - 7.0f, y - 1.5f);
  plane.close();
  canvas->drawPath(plane.detach(), fill);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::eye, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const auto p = pen(colour, alpha, 1.7f);
  const float x = box.centerX(), y = box.centerY();
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 10.0f, y - 7.0f, x + 10.0f, y + 11.0f), 200.0f, 140.0f, false, p);
  canvas->drawArc(skia::SkRect::MakeLTRB(x - 10.0f, y - 11.0f, x + 10.0f, y + 7.0f), 20.0f, 140.0f, false, p);
  canvas->drawCircle(x, y, 3.0f, p);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::dot which, const skia::SkRect& box, skia::SkColor, float alpha) {
  skia::SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(which.colour);
  fill.setAlphaf(alpha);
  canvas->drawCircle(box.centerX(), box.centerY(), 6.0f, fill);
}
inline void draw_icon(skia::SkCanvas* canvas, icon::minus, const skia::SkRect& box, skia::SkColor colour, float alpha) {
  const float x = box.centerX(), y = box.centerY();
  canvas->drawLine(x - 7.0f, y, x + 7.0f, y, pen(colour, alpha, 2.0f));
}
inline void draw_icon(skia::SkCanvas* canvas, const icon_t& which, const skia::SkRect& box, skia::SkColor colour,
                      float alpha) {
  std::visit([&](auto one) { draw_icon(canvas, one, box, colour, alpha); }, which);
}

// An avatar's colour, the same every time for the same id.
[[nodiscard]] inline skia::SkColor avatar_colour(std::string_view id) {
  static constexpr std::array<skia::SkColor, 7> palette{
      skia::colorSetARGB(255, 229, 115, 115), skia::colorSetARGB(255, 235, 150, 80),
      skia::colorSetARGB(255, 112, 185, 105), skia::colorSetARGB(255, 82, 160, 225),
      skia::colorSetARGB(255, 150, 120, 225), skia::colorSetARGB(255, 80, 185, 190),
      skia::colorSetARGB(255, 220, 110, 170)};
  return palette[std::hash<std::string_view>{}(id) % palette.size()];
}

// A proxy profile's colour, as Gajim gives each its own: the same every
// time for the same name, so an account's choice is known at a glance.
[[nodiscard]] inline skia::SkColor proxy_colour(std::string_view name) { return avatar_colour(name); }

// Up to two letters for an avatar: the first character of each of the first
// two words of a name, or of an address's local part -- in any script, whole
// (a Cyrillic name has Cyrillic initials, not a question mark).
[[nodiscard]] inline std::string initials_of(std::string_view name) {
  if (name.starts_with('@'))
    name.remove_prefix(1);
  name = name.substr(0, name.find_first_of("@:"));
  std::string out;
  int taken = 0;
  bool start = true;
  for (std::size_t at = 0; at < name.size() && taken < 2;) {
    const auto lead = static_cast<unsigned char>(name[at]);
    const std::size_t length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    const bool letter = lead >= 0x80 || std::isalnum(lead) != 0;
    if (letter && start) {
      out += lead < 0x80 ? std::string(1, static_cast<char>(std::toupper(lead))) : std::string(name.substr(at, length));
      ++taken;
    }
    start = !letter;
    at += length;
  }
  return out.empty() ? std::string("?") : out;
}

// Telegram's userpics for those with no picture: a gradient, top to bottom,
// of one of its seven pairs (tdesktop's historyPeerNUserpicBg and Bg2), the
// same for the same id, with the initials in white.
[[nodiscard]] inline std::pair<skia::SkColor, skia::SkColor> userpic_colours(std::string_view id) {
  static constexpr std::array<std::pair<skia::SkColor, skia::SkColor>, 7> pairs{{
      {skia::colorSetARGB(255, 0xff, 0x84, 0x5e), skia::colorSetARGB(255, 0xd4, 0x52, 0x46)},  // red
      {skia::colorSetARGB(255, 0xfe, 0xbb, 0x5b), skia::colorSetARGB(255, 0xf6, 0x81, 0x36)},  // orange
      {skia::colorSetARGB(255, 0xb6, 0x94, 0xf9), skia::colorSetARGB(255, 0x6c, 0x61, 0xdf)},  // violet
      {skia::colorSetARGB(255, 0x9a, 0xd1, 0x64), skia::colorSetARGB(255, 0x46, 0xba, 0x43)},  // green
      {skia::colorSetARGB(255, 0x5b, 0xcb, 0xe3), skia::colorSetARGB(255, 0x35, 0x9a, 0xd4)},  // cyan
      {skia::colorSetARGB(255, 0x5c, 0xaf, 0xfa), skia::colorSetARGB(255, 0x40, 0x8a, 0xcf)},  // blue
      {skia::colorSetARGB(255, 0xff, 0x8a, 0xac), skia::colorSetARGB(255, 0xd9, 0x55, 0x74)},  // pink
  }};
  return pairs[std::hash<std::string_view>{}(id) % pairs.size()];
}

// A round avatar: the colour of `id`, and the initials of `name` in it.
// The pictures fetched for avatars, decoded, by what they are of: a chat's
// id, a person's. Least recently drawn first out, past a number of bytes;
// what is out is read from the disk again when it is wanted.
class avatar_cache {
 public:
  // Held to this many bytes; set from Storage.
  std::size_t budget = 32u << 20;
  void clear() {
    images_.clear();
    order_.clear();
    bytes_ = 0;
  }

  // A picture, counted as used now.
  [[nodiscard]] const skia::Sp<skia::SkImage>* find(std::string_view key) {
    const auto found = images_.find(key);
    if (found == images_.end())
      return nullptr;
    order_.splice(order_.begin(), order_, found->second.used);
    return &found->second.image;
  }
  [[nodiscard]] bool has(std::string_view key) const { return images_.contains(key); }
  void put(std::string key, skia::Sp<skia::SkImage> image) {
    if (!image)
      return;
    if (const auto found = images_.find(key); found != images_.end()) {
      bytes_ -= found->second.bytes;
      order_.erase(found->second.used);
      images_.erase(found);
    }
    const std::size_t size = static_cast<std::size_t>(image->width()) * static_cast<std::size_t>(image->height()) * 4u;
    order_.push_front(key);
    images_.emplace(std::move(key), entry{std::move(image), order_.begin(), size});
    bytes_ += size;
    while (bytes_ > budget && order_.size() > 1) {
      const auto oldest = images_.find(order_.back());
      bytes_ -= oldest->second.bytes;
      images_.erase(oldest);
      order_.pop_back();
    }
  }

 private:
  struct entry {
    skia::Sp<skia::SkImage> image;
    std::list<std::string>::iterator used;
    std::size_t bytes = 0;
  };
  std::list<std::string> order_;  // the most recently used first
  std::map<std::string, entry, std::less<>> images_;
  std::size_t bytes_ = 0;
};
inline avatar_cache& avatar_images() {
  static avatar_cache images;
  return images;
}

inline void draw_avatar(skia::SkCanvas* canvas, const skia::SkRect& disc, std::string_view id, std::string_view name,
                        float alpha) {
  if (const skia::Sp<skia::SkImage>* found = avatar_images().find(id); found && *found) {
    const int saved = canvas->save();
    canvas->clipRRect(skia::SkRRect::MakeOval(disc), true);
    skia::SkPaint paint;
    paint.setAlphaf(alpha);
    canvas->drawImageRect(*found, disc, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    canvas->restoreToCount(saved);
    return;
  }
  skia::SkFont* font = skiff::paint::defaultFont();
  if (font == nullptr)
    return;
  const skiff::paint::Painter p(canvas, *font);
  const auto [top, bottom] = userpic_colours(id);
  const int saved = canvas->save();
  canvas->clipRRect(skia::SkRRect::MakeOval(disc), true);
  skiff::paint::verticalGradient(canvas, disc, top, bottom, alpha);
  canvas->restoreToCount(saved);
  const std::string letters = initials_of(name);
  const float size = disc.width() * 0.4f;
  const float width = p.measure(letters, size, true);
  p.textIn(disc, letters, size, skia::colorSetARGB(255, 255, 255, 255), alpha, true, (disc.width() - width) * 0.5f);
}

// A line of a list or a menu, as wide as what holds it and square: an icon
// on the left, its text, and a radio mark on the right where it is one of a
// choice. It lights under the pointer; a press does `act`.
// An icon on its own, in a row: drawn, not pressed.
struct icon_mark : scene::Node {
  icon_t icon;
  skia::SkColor colour = dim_colour;
  explicit icon_mark(icon_t mark = icon::none{}) : icon(mark) {
    fState.apply({.width = 28.0f, .height = 36.0f, .alignSelf = scene::align::kMiddle});
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) { draw_icon(canvas, icon, fState.fBounds, colour, alpha); }
};
// A radio's ring, with a dot in it while it is the one chosen.
struct radio_mark : scene::Node {
  bool on = false;
  radio_mark() { fState.apply({.width = 20.0f, .height = 20.0f, .alignSelf = scene::align::kMiddle}); }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    const float x = fState.fBounds.centerX(), y = fState.fBounds.centerY();
    canvas->drawCircle(x, y, 8.0f, pen(on ? accent_colour : dim_colour, alpha, 2.0f));
    if (skia::SkFont* font = skiff::paint::defaultFont(); font && on)
      skiff::paint::Painter(canvas, *font)
          .fillRounded(skia::SkRect::MakeLTRB(x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f), 4.0f, accent_colour, alpha);
  }
};
// A round avatar of a size, in a row.
struct avatar_mark : scene::Node {
  std::string key;
  std::string name;
  avatar_mark(std::string id, std::string shown, float size) : key(std::move(id)), name(std::move(shown)) {
    fState.apply({.width = size, .height = size, .alignSelf = scene::align::kMiddle});
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) { draw_avatar(canvas, fState.fBounds, key, name, alpha); }
};
// A name over how it is: two lines, each cut where it runs out of room,
// taking what their row leaves them.
struct two_lines : nodes::Stack {
  nodes::Text name;
  nodes::Text state;
  two_lines(std::string first, std::string second, float size, float gap)
      : name(std::move(first), size, text_colour, true), state(std::move(second), size - 2.0f, dim_colour) {
    this->setGap(gap);
    fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    for (nodes::Text* each : {&name, &state}) {
      each->setElided(true);
      each->apply({.fillX = true});
    }
  }
  void forEachChild(auto&& f) {
    f(name);
    f(state);
  }
};

template <class Act>
struct row_item : nodes::Stack {
  Act act;
  // Whether it is one of a choice, and the chosen one.
  std::optional<bool> radio;
  icon_mark mark;
  nodes::Text label;
  radio_mark dot;

  static constexpr float kHeight = 46.0f;

  // Declared: its icon, its text taking the room, and a radio at the end.
  row_item(std::string text, Act what, icon_t icon = icon::none{}, std::optional<bool> choice = std::nullopt)
      : act(std::move(what)), radio(choice), mark(icon), label(std::move(text), 15.0f, text_colour) {
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    mark.setVisible(!std::holds_alternative<icon::none>(icon));
    label.setElided(true);
    label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    dot.on = choice.value_or(false);
    dot.setVisible(choice.has_value());
  }

  void set_chosen(bool on) {
    radio = on;
    dot.on = on;
    dot.setVisible(true);
    dot.markDamaged();
  }
  // Lit as the line whose page is shown beside the list.
  void set_lit(bool on) {
    lit = on;
    this->markDamaged();
  }
  bool lit = false;

  void forEachChild(auto&& f) {
    f(mark);
    f(label);
    f(dot);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont(); font && (lit || fState.fHovered || this->showsFocus()))
      skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = label.text();
    out.fSelected = radio.value_or(false);
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// A round button with only an icon in it: back, close.
template <class Act>
struct icon_button : scene::Node {
  Act act;
  icon_t icon;
  skia::SkColor colour = text_colour;

  icon_button(icon_t mark, Act what) : act(std::move(what)), icon(mark) {
    fState.apply({.width = 36.0f, .height = 36.0f});
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (fState.fHovered || this->showsFocus())
      p.fillRounded(box, box.width() * 0.5f, chosen_colour, alpha);
    draw_icon(canvas, icon, box, colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// The head of a page: ← on the left where there is somewhere to go back to,
// the page's name, and ✕ on the right where the page closes.
template <class Back, class Close>
struct page_header : nodes::Stack {
  icon_button<Back> back;
  nodes::Text title;
  icon_button<Close> close;

  static constexpr float kHeight = 54.0f;

  page_header(std::string name, Back to, Close shut, bool has_back, bool has_close)
      : back(icon::back{}, std::move(to)), title(std::move(name), 17.0f, text_colour, true),
        close(icon::close{}, std::move(shut)) {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
    back.setVisible(has_back);
    close.setVisible(has_close);
    back.apply({.alignSelf = scene::align::kMiddle});
    close.apply({.alignSelf = scene::align::kMiddle});
    title.setElided(true);
    title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle,
                 .margin = {0.0f, 0.0f, 0.0f, has_back ? 0.0f : 10.0f}});
  }

  void forEachChild(auto&& f) {
    f(back);
    f(title);
    f(close);
  }
};

// One segment of a segmented control: square, its text centred, filled
// with the accent while it is the one chosen.
template <class Act>
struct segment : nodes::Stack {
  Act act;
  bool active = false;
  nodes::Text label;

  segment(std::string text, Act what) : act(std::move(what)), label(std::move(text), 13.0f, text_colour, true) {
    fState.apply({.width = 92.0f, .height = 28.0f});
    fStack.justify = nodes::justify::middle{};
    label.apply({.alignSelf = scene::align::kMiddle});
  }

  void set_active(bool on) {
    active = on;
    label.setColour(on ? on_accent_colour : text_colour);
    this->markDamaged();
  }

  void forEachChild(auto&& f) { f(label); }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    if (active)
      p.fillRounded(fState.fBounds, 0.0f, accent_colour, alpha);
    else if (fState.fHovered || this->showsFocus())
      p.fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::tab{};
    out.fLabel = label.text();
    out.fSelected = active;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// ---- the drawer's button ------------------------------------------------------

// Three lines at the top-left of the conversation list: a press pulls the
// drawer out.
template <class Actions>
struct menu_button : scene::Node {
  Actions* actions = nullptr;

  explicit menu_button(Actions* a) : actions(a) { fState.apply({.width = 36.0f, .height = 36.0f}); }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (fState.fHovered || this->showsFocus())
      p.fillRounded(box, 8.0f, chosen_colour, alpha);
    const float left = box.centerX() - 8.0f;
    for (const float dy : {-6.0f, 0.0f, 6.0f})
      p.fillRounded(skia::SkRect::MakeXYWH(left, box.centerY() + dy - 1.0f, 16.0f, 2.0f), 1.0f, text_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->open_drawer();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = "Menu";
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// ---- the conversations ------------------------------------------------------

// A control that does nothing when pressed: the page already up.
struct nothing {
  void operator()() const {}
};

// A control whose action is still to come: it says so.
template <class Actions>
struct not_yet {
  Actions* actions = nullptr;
  std::string_view what;
  void operator()() const { actions->not_implemented(std::string(what)); }
};

inline skia::SkColor selected_colour = skia::colorSetARGB(255, 43, 82, 120);
inline skia::SkColor band_colour = skia::colorSetARGB(255, 18, 20, 23);
// Between the sections of a panel: just darker than the panel.
inline skia::SkColor section_colour = skia::colorSetARGB(255, 26, 29, 33);
inline skia::SkColor tile_colour = skia::colorSetARGB(255, 40, 45, 50);
inline skia::SkColor bubble_colour = skia::colorSetARGB(255, 33, 41, 52);
inline skia::SkColor sent_time_colour = skia::colorSetARGB(255, 170, 200, 230);

// The colours of a theme, "dark" or "light", put in place: mux.ui's and
// skiff-widgets'. What is made takes its colours then: the window is made
// again after it (window::rebuild).
// The themes, as Telegram Desktop's: their colours are its palettes' own,
// key for key (tools: tdesktop's Resources and lib_ui's colors.palette).
// Classic: tdesktop's base palette.
inline void use_theme(config::theme::classic) {
  background = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  sidebar_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  chosen_colour = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  text_colour = skia::colorSetARGB(255, 0, 0, 0);  // #000000
  dim_colour = skia::colorSetARGB(255, 153, 153, 153);  // #999999
  accent_colour = skia::colorSetARGB(255, 64, 167, 227);  // #40a7e3
  error_colour = skia::colorSetARGB(255, 209, 78, 78);  // #d14e4e
  selected_colour = skia::colorSetARGB(255, 65, 159, 217);  // #419fd9
  selected_text_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  band_colour = skia::colorSetARGB(255, 231, 231, 231);  // #e7e7e7
  section_colour = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  tile_colour = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  bubble_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out_bubble_colour = skia::colorSetARGB(255, 239, 253, 222);  // #effdde
  sent_time_colour = skia::colorSetARGB(255, 109, 181, 102);  // #6db566
  chat_colour = skia::colorSetARGB(255, 155, 212, 148);  // #9bd494
  on_accent_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  auto& widget = widgets::theme();
  widget = widgets::Theme{};
  widget.fSurface = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceHover = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceActive = skia::colorSetARGB(255, 229, 229, 229);
  widget.fText = skia::colorSetARGB(255, 0, 0, 0);
  widget.fLabel = skia::colorSetARGB(255, 0, 0, 0);
  widget.fTextDim = skia::colorSetARGB(255, 153, 153, 153);
  widget.fTextFaint = skia::colorSetARGB(255, 153, 153, 153);
  widget.fAccent = skia::colorSetARGB(255, 64, 167, 227);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
}
// Day: tdesktop's day-blue.tdesktop-theme.
inline void use_theme(config::theme::day) {
  background = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  sidebar_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  chosen_colour = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  text_colour = skia::colorSetARGB(255, 0, 0, 0);  // #000000
  dim_colour = skia::colorSetARGB(255, 153, 153, 153);  // #999999
  accent_colour = skia::colorSetARGB(255, 64, 167, 227);  // #40a7e3
  error_colour = skia::colorSetARGB(255, 209, 78, 78);  // #d14e4e
  selected_colour = skia::colorSetARGB(255, 65, 159, 217);  // #419fd9
  selected_text_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  band_colour = skia::colorSetARGB(255, 231, 231, 231);  // #e7e7e7
  section_colour = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  tile_colour = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  bubble_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out_bubble_colour = skia::colorSetARGB(255, 222, 241, 253);  // #def1fd
  sent_time_colour = skia::colorSetARGB(255, 134, 168, 194);  // #86a8c2
  chat_colour = skia::colorSetARGB(255, 116, 180, 224);  // #74b4e0
  on_accent_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  auto& widget = widgets::theme();
  widget = widgets::Theme{};
  widget.fSurface = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceHover = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceActive = skia::colorSetARGB(255, 229, 229, 229);
  widget.fText = skia::colorSetARGB(255, 0, 0, 0);
  widget.fLabel = skia::colorSetARGB(255, 0, 0, 0);
  widget.fTextDim = skia::colorSetARGB(255, 153, 153, 153);
  widget.fTextFaint = skia::colorSetARGB(255, 153, 153, 153);
  widget.fAccent = skia::colorSetARGB(255, 64, 167, 227);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
}
// Tinted: tdesktop's night.tdesktop-theme.
inline void use_theme(config::theme::tinted) {
  background = skia::colorSetARGB(255, 23, 33, 43);  // #17212b
  sidebar_colour = skia::colorSetARGB(255, 23, 33, 43);  // #17212b
  chosen_colour = skia::colorSetARGB(255, 32, 43, 54);  // #202b36
  text_colour = skia::colorSetARGB(255, 245, 245, 245);  // #f5f5f5
  dim_colour = skia::colorSetARGB(255, 112, 132, 153);  // #708499
  accent_colour = skia::colorSetARGB(255, 82, 136, 193);  // #5288c1
  error_colour = skia::colorSetARGB(255, 236, 57, 66);  // #ec3942
  selected_colour = skia::colorSetARGB(255, 43, 82, 120);  // #2b5278
  selected_text_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  band_colour = skia::colorSetARGB(255, 36, 48, 61);  // #24303d
  section_colour = skia::colorSetARGB(255, 35, 46, 60);  // #232e3c
  tile_colour = skia::colorSetARGB(255, 36, 47, 61);  // #242f3d
  bubble_colour = skia::colorSetARGB(255, 24, 37, 51);  // #182533
  out_bubble_colour = skia::colorSetARGB(255, 43, 82, 120);  // #2b5278
  sent_time_colour = skia::colorSetARGB(255, 125, 168, 211);  // #7da8d3
  chat_colour = skia::colorSetARGB(255, 14, 22, 33);  // #0e1621
  on_accent_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  auto& widget = widgets::theme();
  widget = widgets::Theme{};
  widget.fSurface = skia::colorSetARGB(255, 36, 47, 61);
  widget.fSurfaceHover = skia::colorSetARGB(255, 35, 46, 60);
  widget.fSurfaceActive = skia::colorSetARGB(255, 36, 48, 61);
  widget.fText = skia::colorSetARGB(255, 245, 245, 245);
  widget.fLabel = skia::colorSetARGB(255, 245, 245, 245);
  widget.fTextDim = skia::colorSetARGB(255, 112, 132, 153);
  widget.fTextFaint = skia::colorSetARGB(255, 112, 132, 153);
  widget.fAccent = skia::colorSetARGB(255, 82, 136, 193);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
}
// Night: tdesktop's night-green.tdesktop-theme.
inline void use_theme(config::theme::night) {
  background = skia::colorSetARGB(255, 40, 46, 51);  // #282e33
  sidebar_colour = skia::colorSetARGB(255, 40, 46, 51);  // #282e33
  chosen_colour = skia::colorSetARGB(255, 53, 60, 67);  // #353c43
  text_colour = skia::colorSetARGB(255, 245, 245, 245);  // #f5f5f5
  dim_colour = skia::colorSetARGB(255, 130, 134, 138);  // #82868a
  accent_colour = skia::colorSetARGB(255, 63, 193, 176);  // #3fc1b0
  error_colour = skia::colorSetARGB(255, 245, 116, 116);  // #f57474
  selected_colour = skia::colorSetARGB(255, 0, 150, 135);  // #009687
  selected_text_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  band_colour = skia::colorSetARGB(255, 63, 72, 80);  // #3f4850
  section_colour = skia::colorSetARGB(255, 49, 59, 67);  // #313b43
  tile_colour = skia::colorSetARGB(255, 61, 68, 75);  // #3d444b
  bubble_colour = skia::colorSetARGB(255, 51, 57, 63);  // #33393f
  out_bubble_colour = skia::colorSetARGB(255, 42, 47, 51);  // #2a2f33
  sent_time_colour = skia::colorSetARGB(255, 115, 127, 135);  // #737f87
  chat_colour = skia::colorSetARGB(255, 24, 25, 29);  // #18191d
  on_accent_colour = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  auto& widget = widgets::theme();
  widget = widgets::Theme{};
  widget.fSurface = skia::colorSetARGB(255, 61, 68, 75);
  widget.fSurfaceHover = skia::colorSetARGB(255, 49, 59, 67);
  widget.fSurfaceActive = skia::colorSetARGB(255, 63, 72, 80);
  widget.fText = skia::colorSetARGB(255, 245, 245, 245);
  widget.fLabel = skia::colorSetARGB(255, 245, 245, 245);
  widget.fTextDim = skia::colorSetARGB(255, 130, 134, 138);
  widget.fTextFaint = skia::colorSetARGB(255, 130, 134, 138);
  widget.fAccent = skia::colorSetARGB(255, 63, 193, 176);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
}
inline void use_theme(const config::theme_t& chosen) {
  std::visit([](auto one) { use_theme(one); }, chosen);
  // The scroll bars, as tdesktop's scrollBarBg and scrollBarBgOver: dark on
  // the light themes, light on the dark.
  const bool light = std::holds_alternative<config::theme::classic>(chosen) ||
                     std::holds_alternative<config::theme::day>(chosen);
  nodes::scrollBarColours() = light ? nodes::ScrollBarColours{skia::colorSetARGB(0x53, 0, 0, 0), skia::colorSetARGB(0x7a, 0, 0, 0)}
                                    : nodes::ScrollBarColours{skia::colorSetARGB(0x53, 255, 255, 255),
                                                              skia::colorSetARGB(0x7a, 255, 255, 255)};
}

// An accent's colour in a theme: Telegram's circles, a shade of their own
// in each (window_themes_embedded.cpp); the theme's own where none is chosen.
[[nodiscard]] inline skia::SkColor colour_of(const config::accent_t& one, const config::theme_t& in) {
  const std::size_t theme = in.index();
  static constexpr std::array<std::array<skia::SkColor, 9>, 4> shades{{
      {skia::colorSetARGB(255, 64, 167, 227), skia::colorSetARGB(255, 69, 188, 231), skia::colorSetARGB(255, 82, 180, 64), skia::colorSetARGB(255, 212, 108, 153), skia::colorSetARGB(255, 223, 138, 73), skia::colorSetARGB(255, 153, 120, 200), skia::colorSetARGB(255, 197, 82, 69), skia::colorSetARGB(255, 104, 123, 152), skia::colorSetARGB(255, 222, 169, 34)},  // classic
      {skia::colorSetARGB(255, 64, 167, 227), skia::colorSetARGB(255, 69, 188, 231), skia::colorSetARGB(255, 82, 180, 64), skia::colorSetARGB(255, 212, 108, 153), skia::colorSetARGB(255, 223, 138, 73), skia::colorSetARGB(255, 153, 120, 200), skia::colorSetARGB(255, 197, 82, 69), skia::colorSetARGB(255, 104, 123, 152), skia::colorSetARGB(255, 222, 169, 34)},  // day
      {skia::colorSetARGB(255, 82, 136, 193), skia::colorSetARGB(255, 88, 191, 232), skia::colorSetARGB(255, 70, 111, 66), skia::colorSetARGB(255, 170, 96, 132), skia::colorSetARGB(255, 164, 109, 60), skia::colorSetARGB(255, 145, 123, 189), skia::colorSetARGB(255, 171, 81, 73), skia::colorSetARGB(255, 105, 123, 151), skia::colorSetARGB(255, 155, 131, 75)},  // tinted
      {skia::colorSetARGB(255, 63, 193, 176), skia::colorSetARGB(255, 96, 168, 231), skia::colorSetARGB(255, 78, 156, 87), skia::colorSetARGB(255, 202, 120, 150), skia::colorSetARGB(255, 204, 146, 92), skia::colorSetARGB(255, 165, 142, 210), skia::colorSetARGB(255, 210, 117, 112), skia::colorSetARGB(255, 123, 135, 153), skia::colorSetARGB(255, 203, 172, 103)},  // night
  }};
  return shades[theme][one.index()];
}
// A theme, and the accent over it.
inline void use_theme(const config::theme_t& chosen, const config::accent_t& accent) {
  use_theme(chosen);
  accent_colour = colour_of(accent, chosen);
  widgets::theme().fAccent = accent_colour;
}


// What a presence says, in a word or two.
[[nodiscard]] inline std::string presence_text(const availability_t& state) {
  return std::visit(overloaded{[](const availability::online&) { return std::string("online"); },
                               [](const availability::chat&) { return std::string("online"); },
                               [](const availability::away&) { return std::string("away"); },
                               [](const availability::extended_away&) { return std::string("away for a while"); },
                               [](const availability::do_not_disturb&) { return std::string("busy"); },
                               [](const availability::offline&) { return std::string("offline"); }},
                    state);
}
[[nodiscard]] inline std::string presence_of(const model& now, const account_id& account, const std::string& contact) {
  const auto found = now.accounts().find(account);
  if (found == now.accounts().end())
    return "offline";
  const auto kept = found->second.presences.find(contact);
  return kept == found->second.presences.end() ? std::string("offline") : presence_text(kept->second.state);
}

[[nodiscard]] inline bool is_group(const conversation& one) {
  return std::visit(overloaded{[](const conversation_kind::direct&) { return false; }, [](const auto&) { return true; }},
                    one.kind);
}

[[nodiscard]] inline std::string display_name(const conversation& one) {
  return one.name.empty() ? one.id.id : one.name;
}

// The name someone goes by in a conversation: as a member of it, or their
// address's local part.
[[nodiscard]] inline std::string sender_name(const conversation& in, std::string_view sender) {
  for (const member& one : in.members)
    if (one.id == sender && !one.name.empty())
      return one.name;
  if (sender.starts_with('@'))
    sender.remove_prefix(1);
  return std::string(sender.substr(0, sender.find_first_of("@:")));
}

// A time of day, as the clock on the wall says it.
[[nodiscard]] inline std::string clock_of(std::chrono::sys_time<std::chrono::milliseconds> at) {
  const std::time_t t =
      std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(at));
  const std::tm* local = std::localtime(&t);
  return local ? std::format("{:02}:{:02}", local->tm_hour, local->tm_min) : std::string();
}

// One chat in the list, as Telegram Desktop draws it: a round avatar, the
// name, the time of the last message, a line of it, and how many are unread.
// A message's HTML (Matrix's org.matrix.custom.html) read into what is
// drawn: its text -- tags gone, line breaks and paragraphs as newlines,
// list items bulleted, quotes marked, the common entities decoded -- and
// its links, each a place to open.
struct formatted {
  std::string text;
  std::vector<std::pair<std::string, std::string>> links;  // what it says, where it goes
  std::vector<nodes::Text::Link> spans;                    // where in the text each is
};
[[nodiscard]] inline formatted read_html(std::string_view html) {
  formatted out;
  std::string open_href;
  std::size_t link_start = 0;
  const auto entity = [](std::string_view name) -> std::string {
    if (name == "amp")
      return "&";
    if (name == "lt")
      return "<";
    if (name == "gt")
      return ">";
    if (name == "quot")
      return "\"";
    if (name == "apos" || name == "#39")
      return "'";
    if (name == "nbsp")
      return " ";
    return "&" + std::string(name) + ";";
  };
  std::size_t at = 0;
  while (at < html.size()) {
    const char c = html[at];
    if (c == '<') {
      const auto end = html.find('>', at);
      if (end == std::string_view::npos)
        break;
      std::string tag(html.substr(at + 1, end - at - 1));
      std::string name;
      for (const char t : tag) {
        if (t == ' ' || t == '/' && !name.empty())
          break;
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(t)));
      }
      if (name == "br" || name == "br/")
        out.text += '\n';
      else if (name == "/p" || name == "/div" || name == "/blockquote" || name == "/li" || name == "/h1" ||
               name == "/h2" || name == "/h3" || name == "/pre")
        out.text += '\n';
      else if (name == "li")
        out.text += "• ";
      else if (name == "blockquote")
        out.text += "│ ";
      else if (name == "mx-reply") {
        // The quoted message a reply carries: not shown twice.
        const auto close = html.find("</mx-reply>", end);
        at = close == std::string_view::npos ? html.size() : close + 11;
        continue;
      } else if (name == "a") {
        const auto href = tag.find("href=");
        if (href != std::string::npos && href + 6 < tag.size()) {
          const char quote = tag[href + 5];
          const auto stop = tag.find(quote, href + 6);
          open_href = tag.substr(href + 6, stop == std::string::npos ? std::string::npos : stop - href - 6);
          link_start = out.text.size();
        }
      } else if (name == "/a" && !open_href.empty()) {
        out.links.emplace_back(out.text.substr(link_start), open_href);
        if (out.text.size() > link_start)
          out.spans.push_back({link_start, out.text.size(), open_href});
        open_href.clear();
      }
      at = end + 1;
    } else if (c == '&') {
      const auto end = html.find(';', at);
      if (end == std::string_view::npos || end - at > 8) {
        out.text += c;
        ++at;
        continue;
      }
      out.text += entity(html.substr(at + 1, end - at - 1));
      at = end + 1;
    } else {
      out.text += c;
      ++at;
    }
  }
  while (!out.text.empty() && out.text.back() == '\n')
    out.text.pop_back();
  return out;
}

// Where the links of a plain text are: what starts with http:// or
// https://, up to a space.
[[nodiscard]] inline std::vector<nodes::Text::Link> link_spans_in(std::string_view text) {
  std::vector<nodes::Text::Link> out;
  for (std::size_t at = 0; at < text.size();) {
    const auto found = std::min(text.find("https://", at), text.find("http://", at));
    if (found == std::string_view::npos)
      break;
    auto end = text.find_first_of(" \n\t", found);
    if (end == std::string_view::npos)
      end = text.size();
    // A sentence's end is not the link's.
    while (end > found && std::string_view(".,;:!?)\"'").contains(text[end - 1]))
      --end;
    out.push_back({found, end, std::string(text.substr(found, end - found))});
    at = std::max(end, found + 1);
  }
  return out;
}

// The links of a plain text: what starts with http:// or https://, up to a
// space.
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> links_in(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> out;
  for (std::size_t at = 0; at < text.size();) {
    const auto found = std::min(text.find("https://", at), text.find("http://", at));
    if (found == std::string_view::npos)
      break;
    auto end = text.find_first_of(" \n\t", found);
    if (end == std::string_view::npos)
      end = text.size();
    const std::string url(text.substr(found, end - found));
    out.emplace_back(url, url);
    at = end;
  }
  return out;
}

template <class Actions>
struct conversation_row : nodes::Stack {
  Actions* actions = nullptr;
  conversation_id id;
  bool chosen = false;
  bool muted = false;
  avatar_mark face;
  // The name and the time over the last message and the unread count.
  struct lines_column : nodes::Stack {
    struct top_line : nodes::Stack {
      nodes::Text name;
      nodes::Text time;
      top_line(std::string shown, bool chosen)
          : name(std::move(shown), 13.0f, chosen ? selected_text_colour : text_colour, true),
            time("", 13.0f, chosen ? selected_text_colour : dim_colour) {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        name.setElided(true);
        name.apply({.grow = scene::axes::kX});
      }
      void forEachChild(auto&& f) {
        f(name);
        f(time);
      }
    } top;
    struct bottom_line : nodes::Stack {
      nodes::Text preview;
      // The chats' unread count, in a pill.
      struct badge : scene::Node {
        std::int64_t count = 0;
        bool chosen = false, muted = false;
        badge(std::int64_t n, bool is_chosen, bool is_muted) : count(n), chosen(is_chosen), muted(is_muted) {
          fState.apply({.height = 21.0f});
        }
        void measure(const skia::SkRect&) {
          if (skia::SkFont* font = skiff::paint::defaultFont())
            fState.fWidth =
                std::max(22.0f, skiff::paint::Painter(nullptr, *font).measure(std::to_string(count), 12.0f, true) + 14.0f);
        }
        void drawSelf(skia::SkCanvas* canvas, float alpha) {
          skia::SkFont* font = skiff::paint::defaultFont();
          if (font == nullptr)
            return;
          const skiff::paint::Painter p(canvas, *font);
          const skia::SkRect& pill = fState.fBounds;
          p.fillRounded(pill, 10.5f,
                        chosen ? selected_text_colour : muted ? dim_colour : accent_colour, alpha);
          const std::string text = std::to_string(count);
          p.textIn(pill, text, 12.0f, chosen ? selected_colour : on_accent_colour, alpha, true,
                   (pill.width() - p.measure(text, 12.0f, true)) * 0.5f);
        }
      } unread;
      bottom_line(std::int64_t count, bool chosen, bool muted)
          : preview("", 13.0f, chosen ? selected_text_colour : dim_colour), unread(count, chosen, muted) {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY});
        preview.setElided(true);
        preview.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
        unread.setVisible(count > 0);
      }
      void forEachChild(auto&& f) {
        f(preview);
        f(unread);
      }
    } bottom;
    lines_column(std::string shown, std::int64_t count, bool chosen, bool muted)
        : top(std::move(shown), chosen), bottom(count, chosen, muted) {
      this->setGap(6.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(top);
      f(bottom);
    }
  } lines;

  static constexpr float kHeight = 62.0f;

  // Declared: the avatar, then the name and time over the last message and
  // how many are unread.
  // What a row shows of its chat: while that is the same, the row is kept.
  struct view {
    std::string name;
    std::optional<message> last;
    std::int64_t unread = 0;
    bool chosen = false, muted = false;
    friend bool operator==(const view&, const view&) = default;
  };
  [[nodiscard]] static view view_of(const conversation& one, bool is_chosen, bool is_muted) {
    return {display_name(one), one.timeline.empty() ? std::nullopt : std::optional<message>(one.timeline.back()),
            one.unread_here(), is_chosen, is_muted};
  }
  view shown;

  conversation_row(Actions* a, const conversation& one, bool is_chosen, bool is_muted)
      : actions(a), id(one.id), chosen(is_chosen), muted(is_muted), shown(view_of(one, is_chosen, is_muted)),
        face(one.id.id, display_name(one), 46.0f),
        lines(display_name(one), one.unread_here(), is_chosen, is_muted) {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 12.0f, 0.0f, 10.0f}});
    if (!one.timeline.empty()) {
      const message& last = one.timeline.back();
      lines.top.time.setText(clock_of(last.at));
      // What it says, as drawn: an HTML one's text, not its tags.
      std::string text = last.redacted ? "(removed)"
                         : last.body.html ? read_html(*last.body.html).text
                                          : last.body.plain;
      std::ranges::replace(text, '\n', ' ');
      if (last.outgoing)
        text = "You: " + text;
      else if (is_group(one))
        text = sender_name(one, last.sender) + ": " + text;
      lines.bottom.preview.setText(std::move(text));
    }
  }

  void forEachChild(auto&& f) {
    f(face);
    f(lines);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    if (chosen)
      p.fillRounded(fState.fBounds, 0.0f, selected_colour, alpha);
    else if (fState.fHovered || this->showsFocus())
      p.fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->choose(id);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = lines.top.name.text();
    out.fSelected = chosen;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// A link under a message: what it says, in the accent. A press on it is
// seen by the messages' list, which opens it.
struct link_line : nodes::Stack {
  std::string url;
  nodes::Text label;
  link_line(std::string said, std::string where) : url(std::move(where)), label("🔗 " + std::move(said), 13.0f, accent_colour) {
    // As wide as it says, up to a bubble's width: cut there.
    fState.apply({.height = 20.0f, .autoSize = scene::axes::kX});
    label.setElided(true);
    label.setMaxWidth(480.0f);
    fState.setCursor(scene::cursor::hand{});
  }
  void forEachChild(auto&& f) { f(label); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// One message, as Telegram Desktop shows it: a rounded bubble, on the right
// and blue for what was sent from here, on the left otherwise; in a group,
// the sender's name in their colour over the first of a run and their
// avatar beside its last; the time in the bubble's corner.
// A picture in a message, as tdesktop sizes one: its own size fitted into
// 430 by 430 (maxMediaSize), no side under 100 (minPhotoSize); rounded, the
// thumbnail drawn when it has come, a plate until then. With no caption,
// the time is on a dark pill over its corner (msgDateImgBg).
struct picture_view : scene::Node {
  std::string source;
  std::string time;  // drawn over it where there is no caption
  static constexpr float kMax = 430.0f, kMin = 100.0f;
  // Its own proportions: as its message says them, and as its picture has
  // them once it has come -- never another's.
  int width = 0, height = 0;
  bool had_picture = false;
  void update(double) {
    if (!had_picture && avatar_images().has("thumb:" + source)) {
      had_picture = true;
      this->invalidateLayout();  // measured again, by the picture's proportions
    }
  }
  picture_view(std::string where, int w, int h) : source(std::move(where)), width(w), height(h) {}
  // Fitted into 430 by 430 and into the room there is, its proportions
  // kept; no side under 100 where the room allows.
  void measure(const skia::SkRect& parent) {
    float w = width > 0 ? static_cast<float>(width) : 320.0f;
    float h = height > 0 ? static_cast<float>(height) : 240.0f;
    if (const skia::Sp<skia::SkImage>* image = avatar_images().find("thumb:" + source); image && *image) {
      // The picture's own proportions, where the message said none or others.
      const float ratio = static_cast<float>((*image)->width()) / static_cast<float>((*image)->height());
      if (width <= 0 || height <= 0 || std::abs(w / h - ratio) > 0.01f)
        h = w / ratio;
    }
    const float room = parent.width() > 0.0f ? parent.width() : kMax;
    const float scale = std::min({1.0f, kMax / w, kMax / h, room / w});
    w *= scale;
    h *= scale;
    if (w < kMin && h < kMin) {
      const float up = std::min(kMin / std::max(w, h), room / w);
      w *= up;
      h *= up;
    }
    fState.fWidth = std::floor(w);
    fState.fHeight = std::floor(h);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    const skia::SkRect& box = fState.fBounds;
    const int saved = canvas->save();
    canvas->clipRRect(skia::SkRRect::MakeRectXY(box, 10.0f, 10.0f), true);
    if (const skia::Sp<skia::SkImage>* image = avatar_images().find("thumb:" + source); image && *image) {
      // Covering the box, cut at the middle where the proportions differ
      // by a rounding: never stretched.
      const float iw = static_cast<float>((*image)->width()), ih = static_cast<float>((*image)->height());
      const float scale = std::max(box.width() / iw, box.height() / ih);
      const float sw = box.width() / scale, sh = box.height() / scale;
      const skia::SkRect from = skia::SkRect::MakeXYWH((iw - sw) * 0.5f, (ih - sh) * 0.5f, sw, sh);
      skia::SkPaint paint;
      paint.setAlphaf(alpha);
      canvas->drawImageRect(*image, from, box, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint,
                            skia::SkCanvas::kFast_SrcRectConstraint);
    } else if (skia::SkFont* font = skiff::paint::defaultFont()) {
      skiff::paint::Painter(canvas, *font).fillRounded(box, 10.0f, tile_colour, alpha);
    }
    canvas->restoreToCount(saved);
    if (!time.empty())
      if (skia::SkFont* font = skiff::paint::defaultFont()) {
        const skiff::paint::Painter p(canvas, *font);
        const float width = p.measure(time, 11.0f) + 16.0f;
        const skia::SkRect pill = skia::SkRect::MakeXYWH(box.fRight - width - 6.0f, box.fBottom - 24.0f, width, 18.0f);
        p.fillRounded(pill, 9.0f, skia::colorSetARGB(0x54, 0, 0, 0), alpha);
        p.textIn(pill, time, 11.0f, skia::colorSetARGB(255, 255, 255, 255), alpha, false, 8.0f);
      }
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// A file in a message, as tdesktop's row: a round icon in the accent, the
// name over its size; pressed, it is saved and opened.
struct file_view : nodes::Stack {
  std::string source;
  struct disc : scene::Node {
    disc() { fState.apply({.width = 44.0f, .height = 44.0f, .alignSelf = scene::align::kMiddle}); }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 22.0f, accent_colour, alpha);
      draw_icon(canvas, icon::clip{}, fState.fBounds, on_accent_colour, alpha);
    }
  } icon;
  two_lines texts;
  [[nodiscard]] static std::string size_text(std::int64_t bytes) {
    if (bytes <= 0)
      return "File";
    if (bytes < 1024)
      return std::format("{} B", bytes);
    if (bytes < 1024 * 1024)
      return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  file_view(std::string where, std::string name, std::int64_t bytes)
      : source(std::move(where)), texts(std::move(name), size_text(bytes), 14.0f, 4.0f) {
    this->setHorizontal();
    this->setGap(11.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .minWidth = 268.0f - 24.0f, .padding = {2.0f, 0.0f, 2.0f, 0.0f}});
    texts.apply({.grow = scene::axes::kNone, .maxWidth = 360.0f});
    texts.name.setMaxWidth(360.0f);
    texts.state.setMaxWidth(360.0f);
    fState.setCursor(scene::cursor::hand{});
  }
  void forEachChild(auto&& f) {
    f(icon);
    f(texts);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

struct message_bubble : nodes::Stack {
  // The message as it was shown, and where in its sender's run: while
  // these are the same, the bubble is kept.
  message said;
  bool first = false, last = false;
  // The message: its id and text, for its menu.
  std::string message_id;
  std::string plain;
  bool outgoing = false;
  std::string sender;

  static constexpr float kPadX = 12.0f;
  static constexpr float kPadY = 7.0f;
  static constexpr float kAvatar = 34.0f;
  static constexpr float kMaxWidth = 480.0f;

  // The sender's avatar, beside the last of their run in a group; the
  // same room, empty, beside the rest.
  avatar_mark face;
  // What it answers: a bar in the sender's colour, beside who and a line.
  struct quote_row : nodes::Stack {
    struct bar : scene::Node {
      skia::SkColor colour;
      explicit bar(skia::SkColor c) : colour(c) { fState.apply({.width = 3.0f, .height = 32.0f}); }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        if (skia::SkFont* font = skiff::paint::defaultFont())
          skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 1.5f, colour, alpha);
      }
    } line;
    // Who said it over a line of it, each cut at the bubble's width.
    struct said_column : nodes::Stack {
      nodes::Text who;
      nodes::Text said;
      said_column(skia::SkColor colour, std::string name, std::string line)
          : who(std::move(name), 13.0f, colour, true), said(std::move(line), 13.0f, dim_colour) {
        this->setGap(1.0f);
        fState.apply({.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
        for (nodes::Text* each : {&who, &said}) {
          each->setElided(true);
          each->setMaxWidth(kMaxWidth - 10.0f);
        }
      }
      void forEachChild(auto&& f) {
        f(who);
        f(said);
      }
    } texts;
    quote_row(skia::SkColor colour, std::string who, std::string said)
        : line(colour), texts(colour, std::move(who), std::move(said)) {
      this->setHorizontal();
      this->setGap(7.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .margin = {2.0f, 0.0f, 4.0f, 0.0f}});
    }
    void forEachChild(auto&& f) {
      f(line);
      f(texts);
    }
  };
  // The bubble: as wide as what it says, up to its largest.
  struct body_column : nodes::Stack {
    bool outgoing = false;
    std::optional<nodes::Text> name;
    std::optional<quote_row> quote;
    std::optional<picture_view> picture;
    std::optional<file_view> file;
    nodes::Text text;
    std::vector<link_line> links;
    std::optional<nodes::Text> reactions;
    nodes::Text time;
    // A flash over it, fading, where it was jumped to.
    skiff::paint::Tween flash{0.0f, 1200.0f};
    [[nodiscard]] bool settling() const { return flash.moving(); }
    // The time goes in the last line of the text where that line leaves room
    // for it, as Telegram's does; on a line of its own where it does not.
    // Decided from the last layout; a change is laid out at the next.
    void update(double now_ms) {
      if (flash.step(now_ms))
        this->markDamaged();
      if (!text.visible() || text.bounds().isEmpty() || links.size() > 0 || reactions)
        return;
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      const float room = fState.contentBox().width();
      const float needs = text.lastLineWidth() + skiff::paint::Painter(nullptr, *font).measure(time.text(), 11.0f) + 10.0f;
      const bool inline_time = needs <= room;
      if (inline_time == time.visible())
        time.setVisible(!inline_time);
    }
    void draw(skia::SkCanvas* canvas, float alpha) {
      scene::drawDefault(*this, canvas, alpha);
      if (!time.visible())
        if (skia::SkFont* font = skiff::paint::defaultFont()) {
          const skiff::paint::Painter p(canvas, *font);
          const skia::SkRect inside = fState.contentBox();
          const float width = p.measure(time.text(), 11.0f);
          p.text(time.text(), inside.fRight - width, text.bounds().fBottom - 3.0f, 11.0f, time.colour(), alpha);
        }
      if (flash.value() > 0.0f)
        if (skia::SkFont* font = skiff::paint::defaultFont())
          skiff::paint::Painter(canvas, *font)
              .fillRounded(fState.fBounds, 12.0f, (accent_colour & 0x00FFFFFFu) | (90u << 24), alpha * flash.value());
    }
    body_column(bool mine, std::string said, std::string when)
        : outgoing(mine), text(std::move(said), 13.0f, text_colour), time(std::move(when), 11.0f,
                                                                           mine ? sent_time_colour : dim_colour) {
      this->setGap(2.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .maxWidth = kMaxWidth + 2.0f * kPadX,
                    .padding = {kPadY, kPadX, 5.0f, kPadX}});
      text.setWrapped(true);
      text.setShrinksToLines(true);
      time.apply({.alignSelf = scene::align::kEnd});
    }
    void forEachChild(auto&& f) {
      f(name);
      f(quote);
      f(picture);
      f(file);
      f(text);
      f(links);
      f(reactions);
      f(time);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font)
            .fillRounded(fState.fBounds, 12.0f, outgoing ? out_bubble_colour : bubble_colour, alpha);
    }
  } body;

  // Declared: the avatar's room and the bubble, at the right where it is
  // one's own; the bubble a column of the name, the quote, the text, the
  // links, the reactions and the time.
  message_bubble(const conversation& in, const message& said, bool first_of_run, bool last_of_run)
      : said(said), first(first_of_run), last(last_of_run), message_id(said.id), plain(said.body.plain),
        outgoing(said.outgoing), sender(said.sender), face(said.sender, sender_name(in, said.sender), kAvatar),
        body(said.outgoing, said.redacted ? std::string("(removed)") : said.body.plain + (said.edited ? " (edited)" : ""),
             clock_of(said.at)) {
    this->setHorizontal();
    this->setGap(8.0f);
    // As tdesktop: a sender's messages one under the other nearly touch;
    // where the sender changes, a gap.
    fState.apply({.fillX = true, .autoSize = scene::axes::kY,
                  .padding = {first_of_run ? 8.0f : 1.0f, 0.0f, 1.0f, 0.0f}});
    if (outgoing)
      fStack.justify = nodes::justify::end{};
    const bool group = is_group(in);
    face.setVisible(group && !outgoing);
    face.apply({.alignSelf = scene::align::kEnd});
    if (!(group && !outgoing && last_of_run))
      face.fState.setAlpha(0.0f);  // its room kept, so the run's bubbles line up
    if (group && !outgoing && first_of_run) {
      body.name.emplace(sender_name(in, said.sender), 13.0f, avatar_colour(said.sender), true);
      body.name->setElided(true);
      body.name->setMaxWidth(kMaxWidth);
    }
    // Anyone's words can be selected and copied, as in Telegram.
    body.text.setSelectable(true);
    body.text.setSelectionColour((accent_colour & 0x00FFFFFFu) | (110u << 24));  // the accent, see-through
    std::string when = clock_of(said.at);
    when += std::visit(overloaded{[](const delivery::sending&) { return " · sending"; },
                                  [](const delivery::failed&) { return " · not sent"; },
                                  [](const auto&) { return ""; }},
                       said.delivery);
    body.time.setText(when);
    // What it carries: a picture, sized as tdesktop's; or a file's row.
    if (said.attachment && !said.redacted) {
      const mux::attachment& carried = *said.attachment;
      if (std::holds_alternative<attachment_kind::image>(carried.kind))
        body.picture.emplace(carried.source, carried.width, carried.height);
      else
        body.file.emplace(carried.source, carried.name, carried.size);
      // No caption: the text goes, and a picture has its time over it.
      if (said.body.plain.empty() && !said.body.html) {
        body.text.setVisible(false);
        if (body.picture) {
          body.picture->time = when;
          body.time.setVisible(false);
          body.apply({.padding = {3.0f, 3.0f, 3.0f, 3.0f}});
        }
      }
    }
    // Formatted, it is drawn from its HTML: its text, and its links; plain,
    // its links are the URLs in it.
    // Its links in its text, where they stand: an <a>'s label going where
    // its href says, and the addresses in a plain text.
    if (said.body.html && !said.redacted) {
      auto read = read_html(*said.body.html);
      body.text.setText(read.text + (said.edited ? " (edited)" : ""));
      body.text.setLinks(std::move(read.spans), accent_colour);
    } else if (!said.redacted) {
      body.text.setLinks(link_spans_in(said.body.plain), accent_colour);
    }
    if (said.replies_to) {
      const auto found = std::ranges::find(in.timeline, *said.replies_to, &message::id);
      const bool known = found != in.timeline.end();
      std::string line = known ? found->body.plain : std::string("not loaded");
      std::ranges::replace(line, '\n', ' ');
      body.quote.emplace(known ? avatar_colour(found->sender) : accent_colour,
                         known ? (found->outgoing ? std::string("You") : sender_name(in, found->sender))
                               : std::string("A message"),
                         std::move(line));
      body.apply({.minWidth = 160.0f});
    }
    if (!said.reactions.empty()) {
      std::string line;
      for (const auto& [key, who] : said.reactions)
        line += std::format("{} {}  ", key, who.size());
      body.reactions.emplace(std::move(line), 13.0f, dim_colour);
    }
  }

  void forEachChild(auto&& f) {
    f(face);
    f(body);
  }

  // Swiped to the left to answer it: how far, following the pointer, and
  // back to its place when let go. Drawn moved, with the arrow of a reply
  // coming in at the right; where it is laid out does not change.
  skiff::paint::Tween swipe{0.0f, 180.0f, skiff::paint::movement::subtle{}};
  static constexpr float kSwipeToReply = 70.0f;
  [[nodiscard]] bool settling() const { return swipe.moving(); }
  void update(double now_ms) {
    if (swipe.step(now_ms))
      this->markDamaged();
  }
  void draw(skia::SkCanvas* canvas, float alpha) {
    const float shift = swipe.value();
    if (shift == 0.0f) {
      scene::drawDefault(*this, canvas, alpha);
      return;
    }
    const int saved = canvas->save();
    canvas->translate(shift, 0.0f);
    scene::drawDefault(*this, canvas, alpha);
    canvas->restoreToCount(saved);
    const float reached = std::clamp(-shift / kSwipeToReply, 0.0f, 1.0f);
    const skia::SkRect& box = fState.fBounds;
    const skia::SkRect disc = skia::SkRect::MakeXYWH(box.fRight - 34.0f, box.centerY() - 14.0f, 28.0f, 28.0f);
    if (skia::SkFont* font = skiff::paint::defaultFont())
      skiff::paint::Painter(canvas, *font).fillRounded(disc, 14.0f, reached >= 1.0f ? accent_colour : tile_colour,
                                                       alpha * reached);
    draw_icon(canvas, icon::back{}, disc, reached >= 1.0f ? on_accent_colour : dim_colour, alpha * reached);
  }
  // Pressed with the right button, it asks for its menu.
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// Something not there yet, said in a box over the window.
template <class Actions>
struct notice_box : nodes::Stack {
  nodes::Text title;
  nodes::Text note;
  widgets::Button<ask<Actions, &Actions::close_notice>> ok;

  notice_box(Actions* a, std::string heading, std::string text)
      : title(std::move(heading), 17.0f, text_colour, true), note(std::move(text), 14.0f, dim_colour), ok("OK", {a}) {
    fState.apply({.fill = true, .padding = {20.0f, 22.0f, 20.0f, 22.0f}});
    this->setGap(10.0f);
    title.setWrapped(true);
    title.apply({.fillX = true});
    note.setWrapped(true);
    note.apply({.fillX = true});
    ok.setPrimary(true);
    ok.apply({.width = 90.0f, .height = 34.0f, .alignSelf = scene::align::kEnd});
  }
  void forEachChild(auto&& f) {
    f(title);
    f(note);
    f(ok);
  }
};


// What a chat says of itself, over its messages: its avatar, name and who
// is in it or how they are, a line under it, and the button that opens its
// info beside it.
template <class Actions>
struct chat_header : nodes::Stack {
  // What the head shows: the chat's key and name, and how it is -- or no
  // chat. The head is made from it, nothing set in it afterwards.
  struct view {
    std::optional<std::string> key;
    std::string title = "Choose a chat";
    std::string status;
    friend bool operator==(const view&, const view&) = default;
  };
  [[nodiscard]] static view view_of(const conversation* one, const model& now) {
    if (one == nullptr)
      return {};
    const auto count = std::max<std::int64_t>(static_cast<std::int64_t>(one->members.size()), one->member_count);
    std::string about = is_group(*one) ? std::format("{} member{}", count, count == 1 ? "" : "s")
                                       : presence_of(now, one->id.account, one->id.id);
    if (!one->typing.empty())
      about = one->typing.size() == 1 ? sender_name(*one, one->typing.front()) + " is typing…"
                                      : std::format("{} are typing…", one->typing.size());
    return {one->id.id, display_name(*one), std::move(about)};
  }

  // The chat's avatar, its name over how it is, and the button to its info.
  struct head_row : nodes::Stack {
    avatar_mark face;
    two_lines texts;
    icon_button<ask<Actions, &Actions::toggle_info>> info;
    head_row(Actions* a, const view& shown)
        : face(shown.key.value_or(""), shown.title, 38.0f), texts(shown.title, shown.status, 15.0f, 3.0f),
          info(icon::info{}, {a}) {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .grow = scene::axes::kY, .padding = {0.0f, 10.0f, 0.0f, 14.0f}});
      info.apply({.alignSelf = scene::align::kMiddle});
      face.setVisible(shown.key.has_value());
      info.setVisible(shown.key.has_value());
      texts.state.setVisible(shown.key.has_value());
    }
    void forEachChild(auto&& f) {
      f(face);
      f(texts);
      f(info);
    }
  } row;
  nodes::Box<> divider{band_colour};

  static constexpr float kHeight = 56.0f;

  // Declared: the row over a line dividing it from the messages.
  chat_header(Actions* a, const view& shown) : row(a, shown) {
    fState.apply({.fill = true});
    divider.apply({.fillX = true, .height = 1.0f});
  }

  void forEachChild(auto&& f) {
    f(row);
    f(divider);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont())
      skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, sidebar_colour, alpha);
  }
};

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
struct action_tile : nodes::Stack {
  Act act;
  icon_mark mark;
  nodes::Text label;

  // Declared: the icon at the top, the name at the bottom.
  action_tile(std::string text, icon_t icon, Act what = {})
      : act(std::move(what)), mark(icon), label(std::move(text), 12.0f, text_colour) {
    fState.apply({.height = 58.0f, .padding = {6.0f, 0.0f, 8.0f, 0.0f}});
    fStack.justify = nodes::justify::space_between{};
    mark.colour = text_colour;
    mark.apply({.height = 24.0f});
    label.apply({.alignSelf = scene::align::kMiddle});
  }

  void forEachChild(auto&& f) {
    f(mark);
    f(label);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont())
      skiff::paint::Painter(canvas, *font)
          .fillRounded(fState.fBounds, 8.0f, fState.fHovered || this->showsFocus() ? chosen_colour : tile_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    act();
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::button{};
    out.fLabel = label.text();
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// Someone in a group, in its info: avatar, name, how they are, and their
// role in a pill. Pressed, they are shown on a page of their own.
template <class Open>
struct member_row : nodes::Stack {
  // Who it shows and how they are: while the same, the row is kept.
  member who;
  std::string how_shown;
  Open open;
  std::string id;
  std::optional<std::string> role;
  avatar_mark face;
  // Their name, and how they are under it.
  struct texts_column : nodes::Stack {
    nodes::Text name;
    nodes::Text state;
    texts_column(std::string shown, std::string how)
        : name(std::move(shown), 14.0f, text_colour, true), state(std::move(how), 12.0f, dim_colour) {
      this->setGap(4.0f);
      fState.apply({.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (nodes::Text* each : {&name, &state}) {
        each->setElided(true);
        each->apply({.fillX = true});
      }
    }
    void forEachChild(auto&& f) {
      f(name);
      f(state);
    }
  } texts;
  // Their role, in a pill beside their name.
  struct role_pill : scene::Node {
    std::string text;
    explicit role_pill(std::string what) : text(std::move(what)) {
      fState.apply({.height = 20.0f, .alignSelf = scene::align::kStart, .margin = {10.0f, 0.0f, 0.0f, 0.0f}});
    }
    void measure(const skia::SkRect&) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        fState.fWidth = skiff::paint::Painter(nullptr, *font).measure(text, 12.0f) + 16.0f;
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      const skiff::paint::Painter p(canvas, *font);
      p.fillRounded(fState.fBounds, 10.0f, skia::colorSetARGB(255, 62, 52, 96), alpha);
      p.textIn(fState.fBounds, text, 12.0f, skia::colorSetARGB(255, 190, 170, 250), alpha, false, 8.0f);
    }
  } pill;

  // Declared: the avatar, the name over how they are, the role at the end.
  member_row(const member& one, std::string how, Open what)
      : who(one), how_shown(how), open(std::move(what)), id(one.id), role(one.role),
        face(one.id, one.name.empty() ? one.id : one.name, 40.0f),
        texts(one.name.empty() ? one.id : one.name, std::move(how)), pill(one.role.value_or("")) {
    this->setHorizontal();
    this->setGap(12.0f);
    fState.apply({.fillX = true, .height = 54.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}});
    pill.setVisible(one.role.has_value());
  }

  void forEachChild(auto&& f) {
    f(face);
    f(texts);
    f(pill);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont(); font && fState.fHovered)
      skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    open(*this);
    return true;
  }
};

// A round avatar on its own: the chat's, big, over its name.
struct big_avatar : scene::Node {
  std::string key;
  std::string name;
  big_avatar() { fState.apply({.width = 96.0f, .height = 96.0f, .alignSelf = scene::align::kMiddle}); }
  void drawSelf(skia::SkCanvas* canvas, float alpha) { draw_avatar(canvas, fState.fBounds, key, name, alpha); }
};
// An icon on its own, not to be pressed.
struct icon_view : scene::Node {
  icon_t icon;
  explicit icon_view(icon_t mark) : icon(mark) { fState.apply({.width = 28.0f, .height = 36.0f}); }
  void drawSelf(skia::SkCanvas* canvas, float alpha) { draw_icon(canvas, icon, fState.fBounds, dim_colour, alpha); }
};
// A band between sections: just darker than the panel.
inline nodes::Box<> section_band() {
  nodes::Box<> out{section_colour};
  out.apply({.fillX = true, .height = 6.0f, .margin = {6.0f, 0.0f, 6.0f, 0.0f}});
  return out;
}

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
// Declared: a column of these, nothing placed by hand.
template <class Actions>
struct info_panel : nodes::Stack {
  Actions* actions = nullptr;
  account_id account;
  std::string key;
  // The member shown on a page of their own, over the group's, if one is:
  // the panel's own state, which the page is made from.
  std::optional<std::string> person;
  // The members as last shown, and how each is.
  std::vector<std::pair<member, std::string>> shown_members;

  // What a press does, to the panel -- which stays where it is while its
  // pages are made again.
  struct open_person {
    info_panel* panel;
    void operator()(const auto& row) const { panel->open_member(row.id); }
  };
  struct back_to_group {
    info_panel* panel;
    void operator()() const { panel->close_member(); }
  };
  struct message_them {
    Actions* actions;
    info_panel* panel;
    void operator()() const {
      if (panel->person)
        actions->message_person(conversation_id{panel->account, *panel->person});
    }
  };

  // What the upper part shows: a chat's, or one of its members'.
  struct view {
    std::string key;
    std::string name;
    std::string status;
    bool group = false;
    bool muted = false;
    bool of_person = false;
    friend bool operator==(const view&, const view&) = default;
  };

  // The upper part, made from its view: ← where a member is shown, ✕; the
  // big avatar, the name, how it is; the chat's tiles or the member's; its ID.
  struct head : nodes::Stack {
    struct top_row : nodes::Stack {
      icon_button<back_to_group> back;
      nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
      icon_button<ask<Actions, &Actions::toggle_info>> close;
      top_row(Actions* a, info_panel* panel, bool with_back) : back(icon::back{}, {panel}), close(icon::close{}, {a}) {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 0.0f, 8.0f}});
        gap.apply({.height = 1.0f, .grow = scene::axes::kX});
        back.setVisible(with_back);
      }
      void forEachChild(auto&& f) {
        f(back);
        f(gap);
        f(close);
      }
    } top;
    big_avatar avatar;
    nodes::Text name;
    nodes::Text status;
    struct tiles_row : nodes::Stack {
      action_tile<ask<Actions, &Actions::toggle_mute>> mute;
      action_tile<not_yet<Actions>> manage;
      action_tile<ask<Actions, &Actions::leave_chat>> leave;
      tiles_row(Actions* a, bool muted)
          : mute(muted ? "Unmute" : "Mute", icon::bell{}, {a}), manage("Manage", icon::sliders{}, {a, "Managing a chat"}),
            leave("Leave", icon::leave{}, {a}) {
        this->setHorizontal();
        this->setGap(8.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        mute.apply({.grow = scene::axes::kX});
        manage.apply({.grow = scene::axes::kX});
        leave.apply({.grow = scene::axes::kX});
      }
      void forEachChild(auto&& f) {
        f(mute);
        f(manage);
        f(leave);
      }
    };
    // A member's own: a message to them.
    struct person_row : nodes::Stack {
      action_tile<message_them> message;
      person_row(Actions* a, info_panel* panel) : message("Message", icon::send{}, {a, panel}) {
        this->setHorizontal();
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {16.0f, 16.0f, 4.0f, 16.0f}});
        message.apply({.grow = scene::axes::kX});
      }
      void forEachChild(auto&& f) { f(message); }
    };
    std::optional<tiles_row> tiles;
    std::optional<person_row> person_tiles;
    nodes::Box<> band_1 = section_band();
    // The ID, whole -- wrapped, never cut -- and copied when pressed.
    struct id_line : nodes::Stack {
      nodes::Text id;
      nodes::Text label{"ID", 12.0f, dim_colour};
      explicit id_line(std::string text) : id(std::move(text), 14.0f, accent_colour) {
        this->setGap(2.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}});
        fState.setCursor(scene::cursor::hand{});
        id.setWrapped(true);
        id.apply({.fillX = true});
      }
      void forEachChild(auto&& f) {
        f(id);
        f(label);
      }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        if (skia::SkFont* font = skiff::paint::defaultFont(); font && (fState.fHovered || this->showsFocus()))
          skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
      }
      [[nodiscard]] bool acceptsInput() const { return true; }
      [[nodiscard]] bool hoverChangesAppearance() const { return true; }
      [[nodiscard]] bool onClick(float, float) {
        skiff::scene::setClipboardText(id.text());
        label.setText("ID · copied");
        return true;
      }
    } id_text;

    head(Actions* a, info_panel* panel, const view& shown)
        : top(a, panel, shown.of_person), name(shown.name, 17.0f, text_colour, true),
          status(shown.status, 13.0f, dim_colour), id_text(shown.key) {
      this->setGap(2.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      avatar.key = shown.key;
      avatar.name = shown.name;
      if (shown.of_person)
        person_tiles.emplace(a, panel);
      else
        tiles.emplace(a, shown.muted);
      for (nodes::Text* centred : {&name, &status}) {
        centred->setElided(true);
        centred->apply({.alignSelf = scene::align::kMiddle, .margin = {4.0f, 20.0f, 0.0f, 20.0f}});
      }
    }
    void forEachChild(auto&& f) {
      f(top);
      f(avatar);
      f(name);
      f(status);
      f(tiles);
      f(person_tiles);
      f(band_1);
      f(id_text);
    }
  };
  nodes::Memo<view, head> upper;
  nodes::Box<> band_2 = section_band();
  struct members_head : nodes::Stack {
    icon_view people{icon::people{}};
    nodes::Text title;
    icon_button<not_yet<Actions>> add_member;
    members_head(Actions* a, std::size_t count)
        : title(std::format("{} MEMBER{}", count, count == 1 ? "" : "S"), 13.0f, dim_colour, true),
          add_member(icon::add_person{}, {a, "Adding members"}) {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fill = true, .padding = {6.0f, 10.0f, 6.0f, 16.0f}});
      title.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(people);
      f(title);
      f(add_member);
    }
  };
  // The members' head, as a function of how many there are.
  nodes::Memo<std::size_t, members_head> members_header;
  // The members, in a list of their own that scrolls, reconciled: its place
  // and its rows kept while they show the same.
  nodes::ScrollContainer<nodes::Flow<std::vector<member_row<open_person>>>> members{
      nodes::Flow<std::vector<member_row<open_person>>>({.spacingY = 0.0f, .wrap = false}, {})};
  // The group's view, to come back to from a member's page.
  view group_view;

  static constexpr float kWidth = 340.0f;

  explicit info_panel(Actions* a) : actions(a) {
    fState.apply({.masking = true});
    this->setGap(2.0f);
    upper.apply({.fillX = true, .autoSize = scene::axes::kY});
    members_header.apply({.fillX = true, .height = 48.0f});
    members.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(members.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }

  // The chat shown: its view worked out, its members reconciled.
  void show(const conversation& one, const model& now, bool muted) {
    if (one.id.id != key || one.id.account != account)
      person.reset();
    key = one.id.id;
    account = one.id.account;
    const bool group = is_group(one);
    group_view = {one.id.id,
                  display_name(one),
                  group ? std::format("{} member{}", std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count),
                                      std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count) == 1 ? "" : "s")
                        : presence_of(now, one.id.account, one.id.id),
                  group,
                  muted,
                  false};
    shown_members.clear();
    for (const member& each : one.members)
      shown_members.emplace_back(each, presence_of(now, one.id.account, each.id));
    auto& rows = std::get<0>(std::get<0>(members.fChildren).fChildren);
    if (nodes::reconcile(
            rows, shown_members, [](const auto& each) { return each.first.id; },
            [](const member_row<open_person>& row) { return row.id; },
            [&](const auto& each) { return member_row<open_person>(each.first, each.second, open_person{this}); },
            [](const member_row<open_person>& row, const auto& each) {
              return row.who == each.first && row.how_shown == each.second;
            }))
      members.invalidateLayout();
    members_header.show(static_cast<std::size_t>(std::max<std::int64_t>(static_cast<std::int64_t>(one.members.size()), one.member_count)),
                        [this](std::size_t count) { return members_head(actions, count); });
    this->render();
  }
  void open_member(std::string id) {
    person = std::move(id);
    this->render();
  }
  void close_member() {
    person.reset();
    this->render();
  }
  // The upper part as a function of the group's view and the member open.
  void render() {
    view shown = group_view;
    if (person) {
      // Anyone's page: a member's, with how they are and their role; or,
      // for someone the chat does not list -- the other side of a direct
      // chat, a sender from further back -- by their address.
      shown = {*person, *person, std::string("not a member of this chat"), group_view.group, group_view.muted, true};
      if (const auto found = std::ranges::find(shown_members, *person, [](const auto& each) { return each.first.id; });
          found != shown_members.end()) {
        const member& who = found->first;
        shown = {who.id,
                 who.name.empty() ? who.id : who.name,
                 who.role ? std::format("{} · {}", found->second, *who.role) : found->second,
                 group_view.group,
                 group_view.muted,
                 true};
      } else if (!group_view.group && *person == key) {
        shown.name = group_view.name;
        shown.status = group_view.status;
      }
    }
    upper.show(shown, [this](const view& v) { return head(actions, this, v); });
    const bool list = shown.group && !shown.of_person;
    band_2.setVisible(list);
    members_header.setVisible(list);
    members.setVisible(list);
    this->invalidateLayout();
  }

  void forEachChild(auto&& f) {
    f(upper);
    f(band_2);
    f(members_header);
    f(members);
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    p.fillRounded(box, 0.0f, sidebar_colour, alpha);
    p.fillRounded(skia::SkRect::MakeXYWH(box.fLeft, box.fTop, 1.0f, box.height()), 0.0f, band_colour, alpha);
  }
};

// An edge between two parts of the window, to drag: the pointer turns into
// a resize arrow over it, and a drag asks `on_drag(x)` for the edge to be at
// x. It draws a thin line where `with_line`.
template <class OnDrag>
struct drag_edge : scene::Node {
  OnDrag on_drag;
  bool with_line = true;
  bool dragging = false;

  explicit drag_edge(OnDrag what, bool line = true) : on_drag(std::move(what)), with_line(line) {
    fState.setCursor(scene::cursor::resize_horizontal{});
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr || !with_line)
      return;
    const skia::SkRect& box = fState.fBounds;
    skiff::paint::Painter(canvas, *font)
        .fillRounded(skia::SkRect::MakeXYWH(box.centerX() - 0.5f, box.fTop, 1.0f, box.height()), 0.0f, band_colour,
                     alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool focusable() const { return false; }

  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    dragging = true;
    reply.capturePointer();
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) {
    if (!dragging)
      return;
    on_drag(at.x);
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::up&, scene::PointerReply& reply) {
    dragging = false;
    reply.releasePointer();
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::cancel&, scene::PointerReply& reply) {
    dragging = false;
    reply.releasePointer();
  }
};

// Where an edge was dragged to, asked of the program.
template <class Actions>
struct resize_sidebar_to {
  Actions* actions = nullptr;
  void operator()(float x) const { actions->resize_sidebar(x); }
};
template <class Actions>
struct resize_info_to {
  Actions* actions = nullptr;
  void operator()(float x) const { actions->resize_info(x); }
};

// ---- the message field --------------------------------------------------------------

// What Enter in the message field does: asks for its text to be sent.
template <class Actions>
struct submit_message {
  Actions* actions = nullptr;
  void operator()(std::string_view text) const { actions->submit_message(std::string(text)); }
};

// Where a message is written, across the bottom of a chat as in Telegram
// Desktop: a line over it, a paperclip on the left, the text growing with
// what is written, and the send arrow on the right.
template <class Actions>
struct composer_bar : nodes::Stack {
  nodes::Box<> divider{band_colour};
  // What is written answers or edits: said over the field, and ✕ to go back
  // to a plain message.
  struct context_row : nodes::Stack {
    nodes::Text context{"", 13.0f, accent_colour};
    icon_button<ask<Actions, &Actions::cancel_compose>> cancel;
    explicit context_row(Actions* a) : cancel(icon::close{}, {a}) {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 30.0f, .padding = {0.0f, 8.0f, 0.0f, 50.0f}});
      context.setElided(true);
      context.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      cancel.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(context);
      f(cancel);
    }
  } context_line;
  // The paperclip, the field growing with what is written in it, the arrow.
  struct input_row : nodes::Stack {
    icon_button<ask<Actions, &Actions::attach_files>> attach;
    widgets::TextArea<submit_message<Actions>> field;
    icon_button<ask<Actions, &Actions::send_typed>> send;
    explicit input_row(Actions* a)
        : attach(icon::clip{}, {a}), field("Write a message…", {a}), send(icon::send{}, {a}) {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {9.0f, 8.0f, 9.0f, 8.0f}});
      attach.apply({.alignSelf = scene::align::kEnd});
      send.apply({.alignSelf = scene::align::kEnd});
      field.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      send.colour = accent_colour;
    }
    void forEachChild(auto&& f) {
      f(attach);
      f(field);
      f(send);
    }
  } input;
  // The old names, for what reads them.
  widgets::TextArea<submit_message<Actions>>& field = input.field;

  // Declared: the divider, the answer's line where there is one, the row.
  explicit composer_bar(Actions* a) : context_line(a), input(a) {
    context_line.setVisible(false);
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    divider.apply({.fillX = true, .height = 1.0f});
  }

  [[nodiscard]] const std::string& text() const { return input.field.text(); }
  // What is written answers or edits something, said; or nothing.
  void show_context(std::optional<std::string> said) {
    context_line.setVisible(said.has_value());
    context_line.context.setText(said.value_or(""));
    this->invalidateLayout();
  }
  void set_text(std::string text) { input.field.setText(std::move(text)); }
  void clear() { input.field.setText({}); }

  void forEachChild(auto&& f) {
    f(divider);
    f(context_line);
    f(input);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont())
      skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, sidebar_colour, alpha);
  }
};

// "↓": back to the newest, with how many came while one read above them.
template <class Actions>
struct jump_button : scene::Node {
  Actions* actions = nullptr;
  int unseen = 0;
  explicit jump_button(Actions* a) : actions(a) {
    fState.apply({.place = scene::anchor::kBottomRight, .x = -18.0f, .y = -12.0f, .width = 42.0f, .height = 42.0f});
  }
  void set_unseen(int count) {
    unseen = count;
    this->markDamaged();
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    p.fillRounded(box, box.width() * 0.5f, fState.fHovered ? chosen_colour : sidebar_colour, alpha);
    p.strokeRounded(box, box.width() * 0.5f, band_colour, 1.0f, alpha);
    const float x = box.centerX(), y = box.centerY() + 2.0f;
    const auto line = pen(text_colour, alpha, 2.0f);
    canvas->drawLine(x - 7.0f, y - 4.0f, x, y + 3.0f, line);
    canvas->drawLine(x, y + 3.0f, x + 7.0f, y - 4.0f, line);
    if (unseen > 0) {
      const std::string count = std::to_string(unseen);
      const float width = std::max(20.0f, p.measure(count, 11.0f, true) + 10.0f);
      const skia::SkRect badge = skia::SkRect::MakeXYWH(x - width * 0.5f, box.fTop - 10.0f, width, 18.0f);
      p.fillRounded(badge, 9.0f, accent_colour, alpha);
      p.textIn(badge, count, 11.0f, on_accent_colour, alpha, true, (width - p.measure(count, 11.0f, true)) * 0.5f);
    }
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->jump_to_end();
    return true;
  }
};

// The messages, and over them, where one has scrolled up from the newest,
// the way back down. Each is placed by its own spec.
// What a message's menu is made from: the message, and what it carries.
struct menu_facts {
  std::string id;
  bool own = false;
  std::string text;    // all of it
  std::string copied;  // what Copy takes: the selection, or all of it
  bool selection = false;
  std::vector<std::string> seen;
  std::optional<std::string> media;  // a picture's or a file's source
  std::string media_name;
  std::string link;  // a link to it, where it has one
  float x = 0.0f, y = 0.0f;
};

template <class Actions>
struct timeline_area : scene::Node {
  nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble>>> timeline{
      nodes::Flow<std::vector<message_bubble>>({.spacingY = 0.0f, .wrap = false}, {})};
  jump_button<Actions> jump;
  Actions* actions = nullptr;
  explicit timeline_area(Actions* a) : jump(a), actions(a) {
    timeline.apply({.fill = true});
    // The room around the messages is inside what scrolls, so the bar is at
    // the window's edge.
    std::get<0>(timeline.fChildren).apply(
        {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 12.0f, 8.0f, 12.0f}});
    jump.setVisible(false);
  }
  void forEachChild(auto&& f) {
    f(timeline);
    f(jump);
  }
  // A message swiped left: watched from above, before the list's scrolling
  // and a text's selecting see the pointer. A press that moves left at once,
  // and more across than up or down, takes the pointer and moves the
  // message; let go past its mark, it is answered; either way it goes back.
  std::optional<std::string> swiping;
  bool swipe_armed = false;
  float swipe_x = 0.0f, swipe_y = 0.0f;
  std::chrono::steady_clock::time_point swipe_pressed{};
  message_bubble* swiped() {
    if (!swiping)
      return nullptr;
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const auto it = std::ranges::find(entries, *swiping, &message_bubble::message_id);
    return it == entries.end() ? nullptr : &*it;
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::down& press, scene::PointerReply&)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    swipe_armed = press.button <= 1;
    swipe_x = press.x;
    swipe_y = press.y;
    swipe_pressed = std::chrono::steady_clock::now();
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::move& at, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    if (message_bubble* one = this->swiped()) {
      one->swipe.jump(std::clamp(at.x - swipe_x, -120.0f, 0.0f));
      one->markDamaged();
      reply.handle();
      return;
    }
    if (!swipe_armed)
      return;
    const float dx = at.x - swipe_x, dy = at.y - swipe_y;
    if (std::abs(dx) < 8.0f && std::abs(dy) < 8.0f)
      return;
    swipe_armed = false;
    if (dx >= 0.0f || std::abs(dx) < 2.0f * std::abs(dy) ||
        std::chrono::steady_clock::now() - swipe_pressed > std::chrono::milliseconds(250))
      return;
    for (message_bubble& one : std::get<0>(std::get<0>(timeline.fChildren).fChildren))
      if (one.bounds().contains(swipe_x, swipe_y) && !one.message_id.empty()) {
        swiping = one.message_id;
        one.swipe.jump(std::clamp(dx, -120.0f, 0.0f));
        reply.capturePointer();
        reply.suppressHover();
        reply.handle();
        return;
      }
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::up&, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    swipe_armed = false;
    if (message_bubble* one = this->swiped()) {
      if (one->swipe.value() <= -message_bubble::kSwipeToReply)
        actions->reply_to(one->message_id, one->plain);
      one->swipe.setTarget(0.0f);
      swiping.reset();
      reply.releasePointer();
      reply.handle();
    }
  }
  template <class Phase>
  void onPointer(const Phase&, const scene::pointer::cancel&, scene::PointerReply& reply)
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  {
    swipe_armed = false;
    if (message_bubble* one = this->swiped()) {
      one->swipe.setTarget(0.0f);
      swiping.reset();
      reply.releasePointer();
    }
  }

  // Who has read a message: those whose receipt is for it or for one after
  // it, by their names in the chat -- its sender and the user aside.
  const model* seen_model = nullptr;
  std::optional<conversation_id> seen_chat;
  std::vector<std::string> seen_by(const std::string& id, const std::string& sender) const {
    std::vector<std::string> out;
    const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr;
    if (!chat)
      return out;
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < chat->timeline.size(); ++i)
      at.emplace(chat->timeline[i].id, i);
    const auto mine = at.find(id);
    if (mine == at.end())
      return out;
    for (const auto& [user, event] : chat->read_by) {
      if (user == sender || user == chat->id.account.address)
        continue;
      if (const auto theirs = at.find(event); theirs != at.end() && theirs->second >= mine->second)
        out.push_back(sender_name(*chat, user));
    }
    std::ranges::sort(out);
    return out;
  }

  // A right press on a message: its menu, where it was pressed.
  using Node::onPointer;
  // A press on what is in a message -- a picture, a file, a reply's quote,
  // its sender -- as a click: it comes here from what was pressed when that
  // did not take it, at once or, in a list that scrolls, on the release.
  [[nodiscard]] bool onClick(float x, float y) {
    const struct {
      float x, y;
    } press{x, y};
      for (const message_bubble& one : std::get<0>(std::get<0>(timeline.fChildren).fChildren)) {
        // A picture: seen whole. A file: saved and opened.
        if (one.body.picture && one.body.picture->bounds().contains(press.x, press.y)) {
          const conversation* chat = seen_model && seen_chat ? seen_model->find(*seen_chat) : nullptr;
          const auto day = std::chrono::floor<std::chrono::days>(one.said.at);
          actions->open_picture(one.body.picture->source, one.sender,
                                chat ? sender_name(*chat, one.sender) : one.sender,
                                std::format("{:%d.%m.%Y} at {}", std::chrono::year_month_day{day}, clock_of(one.said.at)));
          return true;
        }
        if (one.body.file && one.body.file->bounds().contains(press.x, press.y) && one.said.attachment) {
          actions->open_file(one.body.file->source, one.said.attachment->name);
          return true;
        }
        // The quote: to the message it quotes.
        if (one.body.quote && one.said.replies_to && one.body.quote->bounds().contains(press.x, press.y)) {
          actions->jump_to_message(*one.said.replies_to);
          return true;
        }
        // The sender, by their avatar or their name: their page.
        if ((one.face.visible() && one.face.fState.fAlpha > 0.0f && one.face.bounds().contains(press.x, press.y)) ||
            (one.body.name && one.body.name->bounds().contains(press.x, press.y))) {
          actions->open_member_info(one.sender);
          return true;
        }
      }
    return false;
  }
  void onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button == 1)
      return;  // the main button's presses come as clicks, above
    if (press.button != 3)
      return;
    // Whichever message's row the press is in -- its text, its bubble or the
    // room beside it. What Copy takes is what is selected in it, if anything
    // is, and all of it if not.
    for (const message_bubble& one : std::get<0>(std::get<0>(timeline.fChildren).fChildren))
      if (one.bounds().contains(press.x, press.y)) {
        menu_facts facts;
        facts.id = one.message_id;
        facts.own = one.outgoing;
        facts.text = one.plain;
        facts.selection = one.body.text.hasSelection();
        facts.copied = facts.selection ? one.body.text.selected() : one.plain;
        facts.seen = this->seen_by(one.message_id, one.sender);
        if (one.said.attachment) {
          facts.media = one.said.attachment->source;
          facts.media_name = one.said.attachment->name;
        }
        // A Matrix message's link: matrix.to, to it in its room.
        if (seen_chat && std::holds_alternative<protocol::matrix>(seen_chat->account.speaks) &&
            one.message_id.starts_with('$'))
          facts.link = std::format("https://matrix.to/#/{}/{}", seen_chat->id, one.message_id);
        facts.x = press.x;
        facts.y = press.y;
        actions->message_menu(std::move(facts));
        reply.handle();
        return;
      }
  }
};

// What the chat list shows: all the chats, those of a Matrix space, or
// those of an XMPP roster group.
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
}  // namespace folder
using folder_t = std::variant<folder::all, folder::space, folder::group>;

// A folder's tab over the chat list, as Telegram's: its name, and under
// the one chosen a line in the accent.
template <class Pick>
struct folder_tab : scene::Node {
  Pick pick;
  folder_t which;
  bool chosen = false;
  nodes::Text label;
  folder_tab(std::string name, folder_t what, bool is_chosen, Pick act)
      : pick(std::move(act)), which(std::move(what)), chosen(is_chosen),
        label(std::move(name), 13.0f, is_chosen ? accent_colour : dim_colour, true) {
    fState.apply({.height = 32.0f, .autoSize = scene::axes::kX, .padding = {0.0f, 10.0f, 0.0f, 10.0f}});
    label.setMaxWidth(160.0f);
    label.setElided(true);
    label.apply({.anchor = scene::anchor::kCentreLeft, .origin = scene::anchor::kCentreLeft});
  }
  void forEachChild(auto&& f) { f(label); }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (fState.fHovered || this->showsFocus())
      p.fillRounded(box, 6.0f, chosen_colour, alpha);
    if (chosen)
      p.fillRounded(skia::SkRect::MakeLTRB(box.fLeft + 6.0f, box.fBottom - 3.0f, box.fRight - 6.0f, box.fBottom), 1.5f,
                    accent_colour, alpha);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool focusChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    pick(which);
    return true;
  }
};

template <class Actions>
struct conversations_screen : nodes::Stack {
  Actions* actions = nullptr;
  std::optional<conversation_id> chosen;
  // The account whose chats are listed.
  std::optional<account_id> current;
  bool info_open = false;
  // How wide the chat list and the chat's info are: their own, whatever the
  // window's size, until their edges are dragged.
  float side_width = 300.0f;
  float info_width = 340.0f;

  static constexpr float kMinSidebar = 240.0f;

  // The folder whose chats are listed.
  folder_t folder = folder::all{};
  struct pick_folder {
    conversations_screen* screen;
    void operator()(const folder_t& which) const { screen->choose_folder(which); }
  };
  void choose_folder(const folder_t& which) {
    folder = which;
    if (last_model)
      this->show(*last_model);
  }

  // The chat list: the drawer's button and the name, then the chats.
  struct side_column : nodes::Stack {
    float wanted = 300.0f;
    struct head_row : nodes::Stack {
      menu_button<Actions> menu;
      nodes::Text name{"mux", 17.0f, text_colour, true};
      explicit head_row(Actions* a) : menu(a) {
        this->setHorizontal();
        this->setGap(10.0f);
        fState.apply({.fillX = true, .height = 52.0f, .padding = {8.0f, 8.0f, 8.0f, 8.0f}});
        name.apply({.alignSelf = scene::align::kMiddle});
      }
      void forEachChild(auto&& f) {
        f(menu);
        f(name);
      }
    } head;
    // Search: the chats listed are those whose name or address has what is
    // typed here.
    struct search_box : scene::Node {
      widgets::TextArea<> field{"Search"};
      search_box() {
        fState.apply({.fillX = true, .height = 36.0f, .margin = {0.0f, 10.0f, 8.0f, 10.0f}});
        field.setSingleLine(true);
        field.setFontSize(14.0f);
        field.apply({.fillX = true, .margin = {2.0f, 14.0f, 0.0f, 14.0f}});
      }
      void forEachChild(auto&& f) { f(field); }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        if (skia::SkFont* font = skiff::paint::defaultFont())
          skiff::paint::Painter(canvas, *font)
              .fillRounded(fState.fBounds, 18.0f, field.focused() ? chosen_colour : tile_colour, alpha);
      }
    } search;
    // The folders, where the account has spaces or groups: a line of tabs.
    nodes::Flow<std::vector<folder_tab<pick_folder>>> folders{
        {.direction = nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f}, {}};
    nodes::Text no_chats{"No chats yet.", 13.0f, dim_colour};
    nodes::ScrollContainer<nodes::Flow<std::vector<conversation_row<Actions>>>> list{
        nodes::Flow<std::vector<conversation_row<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
    explicit side_column(Actions* a) : head(a) {
      fState.apply({.fillY = true});
      no_chats.apply({.margin = {12.0f, 16.0f, 0.0f, 16.0f}});
      folders.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {0.0f, 8.0f, 6.0f, 8.0f}});
      list.apply({.fillX = true, .grow = scene::axes::kY});
      std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    }
    // Its own width, as far as the window has room for it.
    void measure(const skia::SkRect& parent) {
      fState.fWidth = std::clamp(wanted, std::min(kMinSidebar, parent.width()),
                                 std::max(kMinSidebar, parent.width() * 0.6f));
    }
    void forEachChild(auto&& f) {
      f(head);
      f(search);
      f(folders);
      f(no_chats);
      f(list);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, sidebar_colour, alpha);
    }
  } side;
  drag_edge<resize_sidebar_to<Actions>> edge;
  // The chat: its header, its messages, and where one writes; or, with no
  // account at all, what to do about it.
  struct chat_column : nodes::Stack {
    // The head, as a function of the chat shown.
    nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>> header;
    timeline_area<Actions> area;
    composer_bar<Actions> line;
    struct empty_state : nodes::Stack {
      nodes::Text title{"No accounts yet", 22.0f, text_colour, true};
      nodes::Text note{"Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, dim_colour};
      widgets::Button<ask<Actions, &Actions::open_new_account>> add;
      explicit empty_state(Actions* a) : add("Add account", {a}) {
        this->setGap(12.0f);
        fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {120.0f, 48.0f, 0.0f, 48.0f}});
        note.setWrapped(true);
        note.apply({.fillX = true});
        add.setPrimary(true);
        add.apply({.width = 140.0f, .height = 36.0f});
      }
      void forEachChild(auto&& f) {
        f(title);
        f(note);
        f(add);
      }
    } empty;
    // No chat chosen: the wallpaper, and in its middle a small pill saying
    // what to do, as tdesktop's (its service message look).
    struct select_hint : nodes::Stack {
      struct pill : scene::Node {
        std::string text = "Select a chat to start messaging";
        pill() { fState.apply({.alignSelf = scene::align::kMiddle}); }
        void measure(const skia::SkRect&) {
          if (skia::SkFont* font = skiff::paint::defaultFont()) {
            fState.fWidth = skiff::paint::Painter(nullptr, *font).measure(text, 13.0f) + 24.0f;
            fState.fHeight = 26.0f;
          }
        }
        void drawSelf(skia::SkCanvas* canvas, float alpha) {
          if (skia::SkFont* font = skiff::paint::defaultFont()) {
            const skiff::paint::Painter p(canvas, *font);
            p.fillRounded(fState.fBounds, 13.0f, skia::colorSetARGB(0x66, 0, 0, 0), alpha);
            p.textIn(fState.fBounds, text, 13.0f, skia::colorSetARGB(255, 255, 255, 255), alpha, false, 12.0f);
          }
        }
      } shown;
      select_hint() {
        fStack.justify = nodes::justify::middle{};
        fState.apply({.fillX = true, .grow = scene::axes::kY});
      }
      void forEachChild(auto&& f) { f(shown); }
    } hint;
    explicit chat_column(Actions* a) : area(a), line(a), empty(a) {
      header.apply({.fillX = true, .height = chat_header<Actions>::kHeight});
      header.show({}, [a](const auto& shown) { return chat_header<Actions>(a, shown); });
      fState.apply({.fillY = true, .grow = scene::axes::kX});
      area.apply({.fillX = true, .grow = scene::axes::kY});
    }
    void forEachChild(auto&& f) {
      f(header);
      f(area);
      f(line);
      f(empty);
      f(hint);
    }
    // The theme's wallpaper, as its colour, behind the messages.
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chat_colour, alpha);
    }
  } chat;
  drag_edge<resize_info_to<Actions>> info_edge;
  info_panel<Actions> info;

  // The old names, for what is kept in the parts.
  nodes::ScrollContainer<nodes::Flow<std::vector<conversation_row<Actions>>>>& list = side.list;
  nodes::Text& no_chats = side.no_chats;
  nodes::Memo<typename chat_header<Actions>::view, chat_header<Actions>>& header = chat.header;
  nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble>>>& timeline = chat.area.timeline;
  // The chat whose messages are shown, how many, and how many came while
  // the view was above the newest.
  std::optional<conversation_id> shown_chat;
  std::string shown_last;
  // Where each chat was scrolled to when it was left: it comes back there.
  std::map<conversation_id, float> scrolled;
  int unseen = 0;
  composer_bar<Actions>& line = chat.line;

  explicit conversations_screen(Actions* a)
      : actions(a), side(a), edge({a}), chat(a), info_edge({a}, false), info(a) {
    fState.apply({.fill = true});
    this->setHorizontal();
    // The edges take a pixel between the columns, their line, and are
    // wider than that over them to be caught.
    edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info_edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info.apply({.fillY = true, .width = info_width});
    this->show_info();
  }

  void forEachChild(auto&& f) {
    f(side);
    f(edge);
    f(chat);
    f(info_edge);
    f(info);
  }

  // The chat list as wide as `x`, where its edge was dragged to.
  void resize_sidebar(float x) {
    side_width = x - fState.contentBox().fLeft;
    side.wanted = side_width;
    side.invalidateLayout();
  }

  // The chat's info as wide as from `x` to the window's right.
  void resize_info(float x) {
    info_width = std::clamp(fState.contentBox().fRight - x, 260.0f,
                            std::max(260.0f, fState.contentBox().width() - side_width - 300.0f));
    info.apply({.width = info_width});
  }

  // The chat's info beside it where it is open and a chat is chosen.
  void show_info() {
    const bool shown = info_open && chosen.has_value();
    info.setVisible(shown);
    info_edge.setVisible(shown);
    side.wanted = side_width;
    info.apply({.width = info_width});
  }

  void toggle_info() {
    info_open = !info_open;
    this->show_info();
  }



  // The model as it is now: the current account's chats, newest first, and
  // the chosen one.
  // The chats muted, as the program keeps them.
  std::set<conversation_id> muted;
  // Where the chosen chat pages back from, and where it was last asked to:
  // scrolled to its top, the older messages are asked for, once for each.
  std::optional<std::string> history_from;
  std::optional<std::string> history_asked;

  [[nodiscard]] bool settling() const { return false; }
  // Back to the newest, and nothing unseen.
  void jump_to_end() {
    // To the newest: the stretch made at the end again.
    if (!made.to_end && last_model) {
      made = made_range{};
      this->show_conversation(*last_model);
    }
    timeline.scrollToEnd();
    unseen = 0;
    chat.area.jump.set_unseen(0);
  }

  // What was searched for last, and the model last shown: typing into the
  // search filters the list again.
  std::string searched;
  const model* last_model = nullptr;

  bool was_typing = false;
  std::string typed_last;
  // Which of the chat's messages are made into bubbles: a stretch of them,
  // at most a few hundred, that slides with the reader -- more made above
  // and the far ones below let go as they scroll up, the other way down --
  // and at the end, follows what comes. Known by the ids at its ends, so
  // history coming in above does not move it. What is made is what the
  // frames walk: a chat of any length costs a few hundred bubbles.
  struct made_range {
    std::optional<std::string> from, to;
    bool to_end = true;
  };
  made_range made;
  std::map<conversation_id, made_range> made_of;
  static constexpr std::size_t kFirstMade = 80, kMostMade = 240, kMadeStep = 60;
  [[nodiscard]] std::pair<std::size_t, std::size_t> made_indices(const std::vector<message>& all) const {
    const auto index_of = [&](const std::optional<std::string>& id) -> std::optional<std::size_t> {
      if (!id)
        return std::nullopt;
      const auto it = std::ranges::find(all, *id, &message::id);
      return it == all.end() ? std::nullopt : std::optional<std::size_t>(static_cast<std::size_t>(it - all.begin()));
    };
    std::size_t to = all.size();
    if (!made.to_end)
      if (const auto at = index_of(made.to))
        to = *at + 1;
    std::size_t from = to > kFirstMade ? to - kFirstMade : 0;
    if (const auto at = index_of(made.from); at && *at <= to)
      from = *at;
    if (to - from > kMostMade)
      from = to - kMostMade;
    return {from, to};
  }
  void set_made(const std::vector<message>& all, std::size_t from, std::size_t to) {
    to = std::min(to, all.size());
    from = std::min(from, to);
    made.from = from < all.size() ? std::optional<std::string>(all[from].id) : std::nullopt;
    made.to = to > 0 ? std::optional<std::string>(all[to - 1].id) : std::nullopt;
    made.to_end = to == all.size();
  }
  // A message to bring into view, once it is made and laid out.
  std::optional<std::string> jumping_to;
  int jump_tries = 0;

  void jump_to(std::string id) {
    if (!chosen || !last_model)
      return;
    const conversation* one = last_model->find(*chosen);
    if (!one)
      return;
    // Made, loaded or paged back to at the next frames, as update() finds it.
    jumping_to = std::move(id);
    jump_tries = 0;
  }

  void update(double) {
    // A message jumped to: made into a bubble where it is loaded, paged back
    // to where it is not -- page after page, as long as there is history --
    // and once it is laid out, brought into view and flashed.
    if (jumping_to && chosen && last_model) {
      auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
      const auto it = std::ranges::find(entries, *jumping_to, &message_bubble::message_id);
      const conversation* one = last_model->find(*chosen);
      if (it != entries.end() && !it->bounds().isEmpty()) {
        const float to = timeline.current() + (it->bounds().fTop - timeline.bounds().fTop) - 60.0f;
        timeline.scrollTo(std::max(0.0f, to));
        it->body.flash.jump(1.0f);
        it->body.flash.setTarget(0.0f);
        it->body.markDamaged();
        jumping_to.reset();
      } else if (one == nullptr) {
        jumping_to.reset();
      } else if (const auto found = std::ranges::find(one->timeline, *jumping_to, &message::id);
                 found != one->timeline.end()) {
        // Made around it, where it is not made already.
        const auto at = static_cast<std::size_t>(found - one->timeline.begin());
        const auto [from, to] = this->made_indices(one->timeline);
        if (at < from || at >= to) {
          this->set_made(one->timeline, at > 40 ? at - 40 : 0, at + 40);
          this->show_conversation(*last_model);
        }
      } else if (history_from && history_asked != history_from) {
        history_asked = history_from;
        actions->load_older(*chosen, *history_from);
      } else if (!history_from && ++jump_tries > 120) {
        jumping_to.reset();  // the beginning, and it was not there
      }
    }
    // What is in the composer: typing while there is text in it.
    if (const bool has_text = !line.text().empty(); has_text != was_typing || (has_text && line.text() != typed_last)) {
      was_typing = has_text;
      typed_last = line.text();
      actions->typing(has_text);
    }
    if (side.search.field.text() != searched && last_model) {
      searched = side.search.field.text();
      this->show(*last_model);
    }
    const bool away = !timeline.atEnd(40.0f);
    if (away != chat.area.jump.visible())
      chat.area.jump.setVisible(away);
    if (!away && unseen != 0) {
      unseen = 0;
      chat.area.jump.set_unseen(0);
    }
    // Near the top: the stretch made slides up -- the far ones below let
    // go -- and past all that is loaded, the history is paged back. Near
    // the bottom, where it is not at the end, it slides down.
    if (chosen && last_model && !jumping_to)
      if (const conversation* one = last_model->find(*chosen)) {
        auto [from, to] = this->made_indices(one->timeline);
        if (timeline.current() <= 300.0f && from > 0) {
          from = from > kMadeStep ? from - kMadeStep : 0;
          to = std::min(to, from + kMostMade);
          this->set_made(one->timeline, from, to);
          this->show_conversation(*last_model);
        } else if (timeline.current() <= 4.0f && from == 0 && history_from && history_asked != history_from) {
          history_asked = history_from;
          actions->load_older(*chosen, *history_from);
        } else if (!made.to_end && timeline.current() >= timeline.extent() - 300.0f) {
          to = std::min(one->timeline.size(), to + kMadeStep);
          from = to > kMostMade && to - from > kMostMade ? to - kMostMade : from;
          this->set_made(one->timeline, from, to);
          this->show_conversation(*last_model);
        }
      }
  }

  void show(const model& now) {
    last_model = &now;
    if (!current || !now.accounts().contains(*current))
      current = now.accounts().empty() ? std::nullopt : std::optional<account_id>(now.accounts().begin()->first);
    auto& rows = std::get<0>(std::get<0>(list.fChildren).fChildren);
    std::vector<const conversation*> chats;
    // What is searched for, in any case: in a name or an address.
    const auto lower = [](std::string text) {
      for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return text;
    };
    const std::string wanted = lower(side.search.field.text());
    // The folders the account has: its spaces, then its groups.
    std::vector<std::pair<std::string, folder_t>> folders{{"All", folder::all{}}};
    if (current) {
      std::set<std::string> groups;
      for (const auto& [key, one] : now.accounts().at(*current).conversations) {
        if (one.space)
          folders.emplace_back(display_name(one), folder::space{one.id.id});
        groups.insert(one.groups.begin(), one.groups.end());
      }
      for (const std::string& name : groups)
        folders.emplace_back(name, folder::group{name});
    }
    if (std::ranges::find(folders, folder, &std::pair<std::string, folder_t>::second) == folders.end())
      folder = folder::all{};
    auto& tabs = std::get<0>(side.folders.fChildren);
    tabs.clear();
    for (auto& [name, which] : folders)
      tabs.emplace_back(name, which, which == folder, pick_folder{this});
    side.folders.setVisible(folders.size() > 1);
    // Whether a chat is in the folder chosen. A space is a folder, not a
    // chat: it is never listed.
    const account* in = current ? &now.accounts().at(*current) : nullptr;
    const auto in_folder = [&](const conversation& one) {
      if (one.space)
        return false;
      return std::visit(overloaded{[](const folder::all&) { return true; },
                                   [&](const folder::space& s) {
                                     const auto found = in->conversations.find(s.room);
                                     return found != in->conversations.end() &&
                                            std::ranges::contains(found->second.children, one.id.id);
                                   },
                                   [&](const folder::group& g) { return std::ranges::contains(one.groups, g.name); }},
                        folder);
    };
    if (in)
      for (const auto& [key, one] : in->conversations)
        if (in_folder(one) &&
            (wanted.empty() || lower(display_name(one)).contains(wanted) || lower(one.id.id).contains(wanted)))
          chats.push_back(&one);
    std::ranges::sort(chats, std::ranges::greater{}, [](const conversation* one) {
      return one->timeline.empty() ? std::chrono::sys_time<std::chrono::milliseconds>{} : one->timeline.back().at;
    });
    // The rows, as a function of the chats: those whose chat shows the same
    // are kept as they are.
    const auto is_chosen = [&](const conversation* one) { return chosen && *chosen == one->id; };
    if (nodes::reconcile(
            rows, chats, [](const conversation* one) { return one->id; },
            [](const conversation_row<Actions>& row) { return row.id; },
            [&](const conversation* one) {
              return conversation_row<Actions>(actions, *one, is_chosen(one), muted.contains(one->id));
            },
            [&](const conversation_row<Actions>& row, const conversation* one) {
              return row.shown == conversation_row<Actions>::view_of(*one, is_chosen(one), muted.contains(one->id));
            }))
      list.invalidateLayout();
    const bool none = now.accounts().empty();
    // The messages' area, not only its list: hidden, it no longer takes the
    // column's height and pushes what is said instead to the bottom.
    // A chat open: its head, messages and composer. None chosen: the hint.
    const bool open = !none && chosen.has_value();
    for (scene::Node* shown : std::initializer_list<scene::Node*>{&header, &chat.area, &line})
      shown->setVisible(open);
    chat.hint.setVisible(!none && !chosen.has_value());
    no_chats.setVisible(!none && chats.empty());
    chat.empty.setVisible(none);
    this->show_info();
    this->invalidateLayout();
    this->show_conversation(now);
  }

  void show_conversation(const model& now) {
    // Whether the reader was at the newest: then the view follows it; and
    // where the view was, for the chat being left.
    const bool was_at_end = timeline.atEnd(40.0f);
    const float left_at = timeline.current();
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    const conversation* one = chosen ? now.find(*chosen) : nullptr;
    header.show(chat_header<Actions>::view_of(one, now),
                [this](const auto& shown) { return chat_header<Actions>(actions, shown); });
    history_from = one ? one->history_from : std::nullopt;
    this->show_info();
    if (!one) {
      entries.clear();
      return;
    }
    info.show(*one, now, muted.contains(one->id));
    chat.area.seen_model = &now;
    chat.area.seen_chat = one->id;
    const auto& all = one->timeline;
    // Where each message is in its sender's run: the first has the name,
    // the last the avatar.
    const auto same = [&](std::size_t i, std::size_t j) {
      return j < all.size() && all[j].sender == all[i].sender && all[j].outgoing == all[i].outgoing;
    };
    const auto first_of_run = [&](std::size_t i) { return i == 0 || !same(i, i - 1); };
    const auto last_of_run = [&](std::size_t i) { return !same(i, i + 1); };
    // A chat shown anew: its stretch as it was left, or its newest.
    if (shown_chat != chosen) {
      if (shown_chat)
        made_of[*shown_chat] = made;
      const auto kept = made_of.find(*chosen);
      made = kept == made_of.end() ? made_range{} : kept->second;
    }
    const auto [first_made, last_made] = this->made_indices(all);
    this->set_made(all, first_made, last_made);
    // The bubbles, as a function of the messages: those that show the same
    // are kept -- with a selection in them -- and only the new are made.
    if (nodes::reconcile(
            entries, std::views::iota(first_made, last_made),
            [&](std::size_t i) { return all[i].id; }, [](const message_bubble& row) { return row.message_id; },
            [&](std::size_t i) { return message_bubble(*one, all[i], first_of_run(i), last_of_run(i)); },
            [&](const message_bubble& row, std::size_t i) {
              return row.said == all[i] && row.first == first_of_run(i) && row.last == last_of_run(i);
            }))
      timeline.invalidateLayout();
    // The newest is at the bottom: the view follows it where the reader was
    // there or the chat is new to the view; otherwise what came after the
    // last one seen is counted on the way down.
    const std::string last = all.empty() ? std::string() : all.back().id;
    if (shown_chat != chosen) {
      if (shown_chat)
        scrolled[*shown_chat] = was_at_end ? -1.0f : left_at;
      const auto kept = scrolled.find(*chosen);
      if (kept == scrolled.end() || kept->second < 0.0f)
        timeline.scrollToEnd(false);  // a chat opened starts at its newest
      else
        timeline.setCurrent(kept->second);
      unseen = 0;
    } else if (was_at_end) {
      timeline.scrollToEnd();
      unseen = 0;
    } else if (last != shown_last) {
      int after = 0;
      for (auto it = all.rbegin(); it != all.rend() && it->id != shown_last; ++it)
        ++after;
      unseen += after;
    }
    chat.area.jump.set_unseen(unseen);
    shown_chat = chosen;
    shown_last = last;
  }
};

// ---- a form row: a caption and a field -----------------------------------

struct field : nodes::Stack {
  nodes::Text caption;
  widgets::TextArea<> box;

  // Declared: the caption over the field, which sits on a plate.
  field(std::string label, std::string placeholder, std::string text = {})
      : caption(std::move(label), 13.0f, dim_colour), box(std::move(placeholder)) {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = 64.0f});
    box.setSingleLine(true);
    box.apply({.fillX = true, .margin = {0.0f, 10.0f, 0.0f, 10.0f}});
    box.setText(std::move(text));
  }
  void forEachChild(auto&& f) {
    f(caption);
    f(box);
  }
  // The plate is where the field is, out to the row's edges.
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    const skia::SkRect& at = box.bounds();
    if (font == nullptr || at.isEmpty())
      return;
    const skia::SkRect plate = skia::SkRect::MakeLTRB(fState.fBounds.fLeft, at.fTop, fState.fBounds.fRight, at.fBottom);
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(plate, 6.0f, tile_colour, alpha);
    p.strokeRounded(plate, 6.0f, box.focused() ? accent_colour : band_colour, 1.0f, alpha);
  }
};

// ---- the account forms ---------------------------------------------------------

// Buttons side by side, as a form ends.
template <class... Buttons>
struct button_row : nodes::Stack {
  std::tuple<Buttons...> buttons;
  explicit button_row(Buttons... all) : buttons(std::move(all)...) {
    this->setHorizontal();
    this->setGap(10.0f);
    fState.apply({.autoSize = scene::axes::kBoth});
  }
  void forEachChild(auto&& f) {
    std::apply([&](auto&... each) { (f(each), ...); }, buttons);
  }
};

// What every account form ends with: what went wrong or what is happening,
// and its buttons. Enter in any of the form's fields submits it.
template <class Actions>
struct form_end : nodes::Stack {
  nodes::Text message{"", 13.0f, error_colour};
  button_row<widgets::Button<ask<Actions, &Actions::submit_login>>, widgets::Button<ask<Actions, &Actions::pop_panel>>>
      buttons;

  form_end(Actions* a, bool editing)
      : buttons(widgets::Button<ask<Actions, &Actions::submit_login>>(editing ? "Save" : "Log in", {a}),
                widgets::Button<ask<Actions, &Actions::pop_panel>>("Close", {a})) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    auto& [submit, close] = buttons.buttons;
    submit.setPrimary(true);
    submit.apply({.width = 120.0f, .height = 36.0f});
    close.apply({.width = 120.0f, .height = 36.0f});
    close.setVisible(editing);
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void forEachChild(auto&& f) {
    f(message);
    f(buttons);
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }
};

// Text typed into an optional: nothing when the field is empty.
[[nodiscard]] inline std::optional<std::string> typed_or_nothing(const std::string& text) {
  if (text.empty())
    return std::nullopt;
  return text;
}

// What "Advanced" folds out on an XMPP form: the resource, where to connect,
// and PLAIN without TLS. Its height is what its last layout took.
template <class Actions>
struct xmpp_advanced : nodes::Stack {
  field resource{"Device name (resource)", "mux", "mux"};
  field host{"Host", "from the domain's SRV records"};
  field port{"Port", "5222"};
  // The switch and what it says, side by side.
  struct plain_row : nodes::Stack {
    widgets::Toggle<ask<Actions, &Actions::toggle_plain>> plain;
    nodes::Text label{"Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                      13.0f, error_colour};
    explicit plain_row(Actions* a) : plain({a}) {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      label.setWrapped(true);
      label.apply({.grow = scene::axes::kX});
    }
    void forEachChild(auto&& f) {
      f(plain);
      f(label);
    }
  } row;
  widgets::Toggle<ask<Actions, &Actions::toggle_plain>>& plain = row.plain;

  explicit xmpp_advanced(Actions* a) : row(a) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
    this->setGap(8.0f);
  }
  void forEachChild(auto&& f) {
    f(resource);
    f(host);
    f(port);
    f(row);
  }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : nodes::Stack {
  Actions* actions = nullptr;
  // The address the account was saved under, when this is an edit of one.
  std::optional<std::string> editing;
  bool advanced = false;

  field address{"Address (JID)", "user@example.com"};
  field password{"Password", "Password"};
  widgets::Button<ask<Actions, &Actions::toggle_advanced>> advanced_button;
  widgets::Collapsible<xmpp_advanced<Actions>> more;
  form_end<Actions> end;

  xmpp_form(Actions* a, const std::optional<config::xmpp_account>& from)
      : actions(a), advanced_button("Advanced", {a}), more(a), end(a, from.has_value()) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.box.setMasked(true);
    advanced_button.apply({.width = 120.0f, .height = 36.0f});
    if (from) {
      editing = from->address;
      address.box.setText(from->address);
      password.box.setText(from->password);
      more.child().resource.box.setText(from->resource);
      if (from->host)
        more.child().host.box.setText(*from->host);
      if (from->port)
        more.child().port.box.setText(std::to_string(*from->port));
      more.child().plain.setOn(from->plain_without_tls);
      advanced = from->resource != "mux" || from->host || from->port || from->plain_without_tls;
    }
    more.setOpenNow(advanced);
  }

  void forEachChild(auto&& f) {
    f(address);
    f(password);
    f(advanced_button);
    f(more);
    f(end);
  }

  // Folded out or away: smoothly, where things move.
  void show_advanced(bool shown) {
    advanced = shown;
    more.setOpen(shown);
  }

  void flip_plain() { more.child().plain.setOn(!more.child().plain.on()); }

  // The account as typed, or what is wrong with it. What is folded away is
  // kept as it is: folding is not clearing.
  [[nodiscard]] std::expected<config::xmpp_account, std::string> account() const {
    config::xmpp_account out{.address = address.box.text(),
                             .password = password.box.text(),
                             .resource = more.child().resource.box.text(),
                             .host = typed_or_nothing(more.child().host.box.text()),
                             .plain_without_tls = more.child().plain.on()};
    if (const std::string& text = more.child().port.box.text(); !text.empty()) {
      std::int64_t number = 0;
      const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (failed != std::errc{} || last != text.data() + text.size())
        return std::unexpected("A port is a number from 1 to 65535");
      out.port = number;
    }
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : nodes::Stack {
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  field user_id{"User ID", "@user:example.org"};
  field password{"Password", "Password"};
  field homeserver{"Homeserver", "found through the server's .well-known"};
  field device_name{"Device name", "mux", "mux"};
  form_end<Actions> end;

  matrix_form(Actions* a, const std::optional<config::matrix_account>& from)
      : actions(a), end(a, from.has_value()) {
    fState.apply({.fillX = true, .autoSize = scene::axes::kY});
    this->setGap(12.0f);
    password.box.setMasked(true);
    if (from) {
      editing = from->user_id;
      user_id.box.setText(from->user_id);
      password.box.setText(from->password);
      if (from->homeserver)
        homeserver.box.setText(*from->homeserver);
      device_name.box.setText(from->device_name);
    }
  }

  void forEachChild(auto&& f) {
    f(user_id);
    f(password);
    f(homeserver);
    f(device_name);
    f(end);
  }

  [[nodiscard]] std::expected<config::matrix_account, std::string> account() const {
    config::matrix_account out{.user_id = user_id.box.text(),
                               .password = password.box.text(),
                               .homeserver = typed_or_nothing(homeserver.box.text()),
                               .device_name = device_name.box.text()};
    if (auto wrong = config::check(out))
      return std::unexpected(*wrong);
    return out;
  }

  void say(std::string text, bool error) { end.say(std::move(text), error); }

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEnter) {
      actions->submit_login();
      reply.handle();
    }
  }

};

// Either form, as the panels hold them.
template <class Actions>
using account_form = std::variant<xmpp_form<Actions>, matrix_form<Actions>>;

// The form of an account's own protocol, filled in from it.
template <class Actions>
[[nodiscard]] account_form<Actions> form_of(Actions* a, const config::account_t& saved) {
  return std::visit(overloaded{[a](const config::xmpp_account& one) {
                                 return account_form<Actions>(std::in_place_index<0>, a, one);
                               },
                               [a](const config::matrix_account& one) {
                                 return account_form<Actions>(std::in_place_index<1>, a, one);
                               }},
                    saved);
}

// The XMPP form among them, when that is the one up.
template <class Actions>
[[nodiscard]] xmpp_form<Actions>* xmpp_form_in(account_form<Actions>& form) {
  return std::visit(overloaded{[](xmpp_form<Actions>& one) { return &one; },
                               [](matrix_form<Actions>&) -> xmpp_form<Actions>* { return nullptr; }},
                    form);
}

// A form laid out in the column under `top`.
template <class Actions>
void place_form(account_form<Actions>& form, const skia::SkRect& column, float top) {
  std::visit(
      [&](auto& one) {
        one.fState.arrange(0.0f, 0.0f);
        scene::layout(one, skia::SkRect::MakeLTRB(column.fLeft, column.fTop + top, column.fRight, column.fBottom));
      },
      form);
}

// Esc closes a panel: back to what is under it.
template <class Actions>
struct closes_on_escape : nodes::Stack {
  Actions* actions = nullptr;
  explicit closes_on_escape(Actions* a) : actions(a) {}

  using Node::onKey;
  void onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply) {
    if (press.key == scene::keys::kEscape) {
      actions->pop_panel();
      reply.handle();
    }
  }
};

// ---- adding an account ------------------------------------------------------------

// A proxy chosen for an account being added: -1 for none.
template <class Actions>
struct choose_new_proxy {
  Actions* actions = nullptr;
  int index = -1;
  void operator()() const { actions->choose_new_proxy(index); }
};

// Adding an account, beside the list of them: XMPP or Matrix at the top, and
// that protocol's form under it.
template <class Actions>
struct add_account_pane : nodes::Stack {
  Actions* actions = nullptr;
  // XMPP | Matrix: two segments in a thin frame.
  struct protocol_switch : nodes::Stack {
    segment<ask<Actions, &Actions::add_xmpp>> xmpp_tab;
    segment<ask<Actions, &Actions::add_matrix>> matrix_tab;
    explicit protocol_switch(Actions* a) : xmpp_tab("XMPP", {a}), matrix_tab("Matrix", {a}) {
      this->setHorizontal();
      this->setGap(1.0f);
      fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}});
    }
    void forEachChild(auto&& f) {
      f(xmpp_tab);
      f(matrix_tab);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      if (skia::SkFont* font = skiff::paint::defaultFont())
        skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
    }
  } tabs;
  nodes::Text note{"", 13.0f, dim_colour};
  // The proxy the new account goes through: none, or one of the profiles.
  struct proxy_row : nodes::Stack {
    nodes::Text title{"Proxy", 13.0f, dim_colour};
    std::vector<segment<choose_new_proxy<Actions>>> choices;
    proxy_row() {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      title.apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(title);
      f(choices);
    }
  } proxies_row;
  account_form<Actions> form;
  std::vector<std::string> proxy_names;
  std::optional<std::string> proxy;
  // The form coming in when the protocol changes, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};

  add_account_pane(Actions* a, const std::vector<config::proxy_settings>& proxies)
      : actions(a), tabs(a), form(std::in_place_index<0>, a, std::nullopt) {
    fState.apply({.fill = true});
    this->setGap(12.0f);
    note.setWrapped(true);
    note.apply({.fillX = true});
    proxies_row.choices.emplace_back("None", choose_new_proxy<Actions>{a, -1});
    for (std::size_t k = 0; k < proxies.size(); ++k) {
      proxy_names.push_back(proxies[k].name);
      proxies_row.choices.emplace_back(proxies[k].name, choose_new_proxy<Actions>{a, static_cast<int>(k)});
    }
    proxies_row.setVisible(!proxies.empty());
    this->set_proxy(-1);
    this->light();
  }

  void forEachChild(auto&& f) {
    f(tabs);
    f(note);
    f(proxies_row);
    f(form);
  }

  void show_xmpp() {
    form.template emplace<0>(this->actions, std::nullopt);
    this->begin_swap();
    this->light();
  }
  void show_matrix() {
    form.template emplace<1>(this->actions, std::nullopt);
    this->begin_swap();
    this->light();
  }
  // The proxy chosen for the new account: -1 for none.
  void set_proxy(int index) {
    proxy.reset();
    if (index >= 0 && static_cast<std::size_t>(index) < proxy_names.size())
      proxy = proxy_names[static_cast<std::size_t>(index)];
    for (std::size_t i = 0; i < proxies_row.choices.size(); ++i)
      proxies_row.choices[i].set_active(static_cast<int>(i) - 1 == index);
  }
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  void fade() {
    const float value = swap.value();
    std::visit([value](auto& one) { one.fState.setAlpha(value); }, form);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
  }

  [[nodiscard]] xmpp_form<Actions>* xmpp() { return xmpp_form_in(form); }

  // The tab of the form that is up, lit, and what that protocol is.
  void light() {
    std::visit(overloaded{[this](const xmpp_form<Actions>&) {
                            tabs.xmpp_tab.set_active(true);
                            tabs.matrix_tab.set_active(false);
                            note.setText("An address like user@example.com, on a server such as Prosody or ejabberd.");
                          },
                          [this](const matrix_form<Actions>&) {
                            tabs.xmpp_tab.set_active(false);
                            tabs.matrix_tab.set_active(true);
                            note.setText("A user ID like @user:example.org, on a homeserver such as Synapse.");
                          }},
               form);
  }
};

// ---- the accounts -------------------------------------------------------------------

// What the model says of a saved account, in a few words, and whether that
// is a failure.
[[nodiscard]] inline std::pair<std::string, bool> state_of(const config::account_t& one, const model& now) {
  const std::string& address = config::address_of(one);
  if (!config::enabled_of(one))
    return {"off", false};
  const auto found = now.accounts().find(account_id{protocol_of(address), address});
  if (found == now.accounts().end())
    return {"offline", false};
  bool failed = false;
  std::string said = std::visit(overloaded{[](const connection::offline&) { return std::string("offline"); },
                                           [](const connection::connecting&) { return std::string("connecting…"); },
                                           [](const connection::online&) { return std::string("online"); },
                                           [&failed](const connection::failed& why) {
                                             failed = true;
                                             return "failed: " + why.error;
                                           }},
                                found->second.state);
  return {std::move(said), failed};
}

// One account in the list: its address, protocol and state. A click shows
// its settings beside the list.
template <class Actions>
struct account_entry : nodes::Stack {
  Actions* actions = nullptr;
  std::string address;
  bool selected = false;
  nodes::Text name;
  nodes::Text state;

  // Declared: its address over its protocol and state, on a plate lit
  // while it is the one chosen.
  account_entry(Actions* a, const config::account_t& saved, const model& now, bool is_selected)
      : actions(a), address(config::address_of(saved)), selected(is_selected),
        name(address, 15.0f, text_colour, true), state("", 13.0f, dim_colour) {
    this->setGap(4.0f);
    fState.apply({.fillX = true, .height = 52.0f, .padding = {7.0f, 10.0f, 7.0f, 10.0f}});
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    for (nodes::Text* each : {&name, &state}) {
      each->setElided(true);
      each->apply({.fillX = true});
    }
  }

  void forEachChild(auto&& f) {
    f(name);
    f(state);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont())
      skiff::paint::Painter(canvas, *font)
          .fillRounded(fState.fBounds, 0.0f, selected ? chosen_colour : sidebar_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->select_account(address);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fSelected = selected;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// The chosen account: on or off, removed, and its own protocol's form.
template <class Actions>
struct account_editor : nodes::Stack {
  // Its address, then on or off and Remove, in a line.
  struct head_row : nodes::Stack {
    nodes::Text heading;
    nodes::Text enabled_label{"On", 13.0f, dim_colour};
    widgets::Toggle<flip_account<Actions>> enabled;
    widgets::Button<remove_account<Actions>> remove;
    head_row(Actions* a, const config::account_t& saved)
        : heading(config::address_of(saved), 20.0f, text_colour, true),
          enabled(flip_account<Actions>{a, config::address_of(saved)}),
          remove("Remove", remove_account<Actions>{a, config::address_of(saved)}) {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      heading.setElided(true);
      heading.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      enabled_label.apply({.alignSelf = scene::align::kMiddle});
      enabled.apply({.alignSelf = scene::align::kMiddle});
      enabled.setOnNow(config::enabled_of(saved));
      remove.apply({.width = 100.0f, .height = 32.0f});
    }
    void forEachChild(auto&& f) {
      f(heading);
      f(enabled_label);
      f(enabled);
      f(remove);
    }
  } head;
  nodes::Text state{"", 13.0f, dim_colour};
  account_form<Actions> form;

  account_editor(Actions* a, const config::account_t& saved) : head(a, saved), form(form_of(a, saved)) {
    fState.apply({.fill = true});
    this->setGap(6.0f);
    state.setElided(true);
    state.apply({.fillX = true, .margin = {0.0f, 0.0f, 14.0f, 0.0f}});
  }

  void forEachChild(auto&& f) {
    f(head);
    f(state);
    f(form);
  }

  // What the model says of it now, kept current without touching the form.
  void show(const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    head.enabled.setOn(config::enabled_of(saved));
  }

  void say(std::string text, bool error) {
    std::visit([&](auto& one) { one.say(std::move(text), error); }, form);
  }
};

// A line with a switch on its right: its text, and the switch.
template <class Act>
struct switch_row : nodes::Stack {
  nodes::Text label;
  widgets::Toggle<Act> toggle;

  // Declared: the text taking the room, the switch at the end.
  switch_row(std::string text, Act what) : label(std::move(text), 15.0f, text_colour), toggle(std::move(what)) {
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = row_item<nothing>::kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    label.setElided(true);
    label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    toggle.apply({.alignSelf = scene::align::kMiddle});
  }
  void forEachChild(auto&& f) {
    f(label);
    f(toggle);
  }
};

// A page of an account's settings chosen from its list.
template <class Actions>
struct choose_account_page {
  Actions* actions = nullptr;
  int page = 0;
  void operator()() const { actions->account_page(page); }
};

// An account's pages, in place of the list of accounts once one is chosen: a
// line for each page of its settings, the one shown lit.
template <class Actions>
struct account_pages : nodes::Stack {
  row_item<choose_account_page<Actions>> connection;
  row_item<choose_account_page<Actions>> privacy;
  row_item<choose_account_page<Actions>> proxy;

  explicit account_pages(Actions* a)
      : connection("Connection", {a, 0}, icon::sliders{}),
        privacy("Privacy", {a, 1}, icon::eye{}),
        proxy("Proxy", {a, 2}, icon::gear{}) {
    fState.apply({.padding = {6.0f, 0.0f, 0.0f, 0.0f}});
    this->light(0);
  }
  void light(int page) {
    connection.set_lit(page == 0);
    privacy.set_lit(page == 1);
    proxy.set_lit(page == 2);
  }
  void forEachChild(auto&& f) {
    f(connection);
    f(privacy);
    f(proxy);
  }
};

// A section's title on a settings page, as Gajim sets them: small, bold, dim.
inline nodes::Text section_title(std::string text) { return nodes::Text(std::move(text), 13.0f, dim_colour, true); }

// An account's Privacy page: whether it sends read receipts.
template <class Actions>
struct account_privacy : nodes::Stack {
  nodes::Text title = section_title("PRIVACY");
  switch_row<ask<Actions, &Actions::flip_account_receipts>> receipts;
  switch_row<ask<Actions, &Actions::flip_account_typing>> typing;
  nodes::Text note{"Off, the people you talk to through this account are not told when you have read their "
                   "messages, or that you are typing. Theirs are still shown, and receipts are still kept here.",
                   13.0f, dim_colour};

  account_privacy(Actions* a, bool receipts_on, bool typing_on)
      : receipts("Send read receipts", {a}), typing("Send typing notifications", {a}) {
    this->setGap(8.0f);
    note.apply({.fillX = true});
    fState.apply({.fill = true});
    note.setWrapped(true);
    receipts.toggle.setOnNow(receipts_on);
    typing.toggle.setOnNow(typing_on);
  }
  void show(bool receipts_on, bool typing_on) {
    receipts.toggle.setOn(receipts_on);
    typing.toggle.setOn(typing_on);
  }
  void say(std::string, bool) {}
  void forEachChild(auto&& f) {
    f(title);
    f(receipts);
    f(typing);
    f(note);
  }
};

// A proxy profile chosen for the chosen account: -1 for none.
template <class Actions>
struct choose_account_proxy {
  Actions* actions = nullptr;
  int index = -1;
  void operator()() const { actions->choose_account_proxy(index); }
};

// An account's Proxy page, as Gajim's: which of the program's proxy profiles
// it connects through, or none; and the way to the profiles themselves.
template <class Actions>
struct account_proxy : nodes::Stack {
  nodes::Text title = section_title("PROXY");
  std::vector<row_item<choose_account_proxy<Actions>>> choices;
  row_item<ask<Actions, &Actions::manage_proxies>> manage;

  account_proxy(Actions* a, const std::vector<config::proxy_settings>& all, const std::optional<std::string>& current)
      : manage("Manage proxies…", {a}, icon::gear{}) {
    title.apply({.margin = {0.0f, 0.0f, 4.0f, 0.0f}});
    manage.apply({.margin = {8.0f, 0.0f, 0.0f, 0.0f}});
    fState.apply({.fill = true});
    // An empty place where the dots are, so the names line up.
    choices.emplace_back("No proxy", choose_account_proxy<Actions>{a, -1}, icon::dot{skia::colorSetARGB(0, 0, 0, 0)},
                         !current.has_value());
    for (std::size_t i = 0; i < all.size(); ++i)
      choices.emplace_back(std::format("{} ({} {}:{})", all[i].name, config::label_of(config::proxy_kind_of(all[i].kind)),
                                       all[i].host, all[i].port),
                           choose_account_proxy<Actions>{a, static_cast<int>(i)}, icon::dot{proxy_colour(all[i].name)},
                           current && *current == all[i].name);
  }
  void show(bool) {}
  void say(std::string, bool) {}
  void forEachChild(auto&& f) {
    f(title);
    f(choices);
    f(manage);
  }
};

// The saved accounts down the side, and the chosen one's settings beside
// them.
template <class Actions>
struct accounts_panel : closes_on_escape<Actions> {
  static constexpr int kTab = 2;
  static constexpr float kListWidth = 280.0f;

  std::optional<std::string> selected;
  // The proxy profiles, for adding an account through one.
  std::vector<config::proxy_settings> proxies;

  // Its ← goes back from an account's pages to the list, and from the list
  // to the chats.
  page_header<ask<Actions, &Actions::accounts_back>, ask<Actions, &Actions::accounts_back>> header;
  // Under the header: the list down the side, and beside it what is chosen.
  struct body_row : nodes::Stack {
    struct side_column : nodes::Stack {
      row_item<ask<Actions, &Actions::open_new_account>> add;
      account_pages<Actions> pages;
      nodes::Text message{"", 13.0f, error_colour};
      nodes::ScrollContainer<nodes::Flow<std::vector<account_entry<Actions>>>> list{
          nodes::Flow<std::vector<account_entry<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
      explicit side_column(Actions* a) : add("Add account", {a}, icon::plus{}), pages(a) {
        fState.apply({.fillY = true, .width = kListWidth});
        pages.setVisible(false);
        pages.apply({.fillX = true, .autoSize = scene::axes::kY});
        message.setWrapped(true);
        message.apply({.fillX = true, .margin = scene::Margin::all(8.0f)});
        list.apply({.fillX = true, .grow = scene::axes::kY});
        std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
      }
      void forEachChild(auto&& f) {
        f(add);
        f(pages);
        f(message);
        f(list);
      }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        if (skia::SkFont* font = skiff::paint::defaultFont())
          skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, sidebar_colour, alpha);
      }
    } side;
    struct detail_column : nodes::Stack {
      // No account chosen, or the chosen one, or adding one.
      std::variant<nodes::Text, account_editor<Actions>, add_account_pane<Actions>, account_privacy<Actions>,
                   account_proxy<Actions>>
          detail{std::in_place_index<0>, "Choose an account.", 15.0f, dim_colour};
      detail_column() {
        fState.apply({.fillY = true, .grow = scene::axes::kX, .padding = {24.0f, 28.0f, 24.0f, 28.0f}});
      }
      void forEachChild(auto&& f) { f(detail); }
    } main;
    explicit body_row(Actions* a) : side(a) {
      this->setHorizontal();
      fState.apply({.fillX = true, .grow = scene::axes::kY});
    }
    void forEachChild(auto&& f) {
      f(side);
      f(main);
    }
  } body;
  row_item<ask<Actions, &Actions::open_new_account>>& add = body.side.add;
  account_pages<Actions>& pages = body.side.pages;
  nodes::Text& message = body.side.message;
  decltype(body.side.list)& list = body.side.list;
  decltype(body.main.detail)& detail = body.main.detail;

  // What is beside the list coming in when another is chosen, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->fade();
  }
  void fade() {
    const float value = swap.value();
    std::visit([value](auto& one) { one.fState.setAlpha(value); }, detail);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->fade();
  }

  explicit accounts_panel(Actions* a)
      : closes_on_escape<Actions>(a), header("Accounts", {a}, {a}, true, false), body(a) {
    this->fState.apply({.fill = true});
  }

  void forEachChild(auto&& f) {
    f(header);
    f(body);
  }

  // The saved accounts, with what the model says of each; the chosen one's
  // form is kept as it is, typing and all.
  void show(const std::vector<config::account_t>& saved, const model& now) {
    auto& entries = std::get<0>(std::get<0>(list.fChildren).fChildren);
    entries.clear();
    const config::account_t* chosen = nullptr;
    for (const config::account_t& one : saved) {
      const bool is_it = selected && config::address_of(one) == *selected;
      if (is_it)
        chosen = &one;
      entries.emplace_back(this->actions, one, now, is_it);
    }
    if (chosen) {
      if (auto* up = this->editor())
        up->show(*chosen, now);
    } else if (!this->adding()) {
      // Nothing to show beside the list: with no account at all, adding one.
      selected.reset();
      if (saved.empty())
        this->show_adding();
      else
        detail.template emplace<0>("Choose an account.", 15.0f, dim_colour);
    }
    this->invalidateLayout();
  }

  // An account's settings, brought up afresh.
  // One of the chosen account's pages beside the list: 0 Connection, 1
  // Privacy, 2 Proxy.
  void show_page(int page, const config::account_t& one, const model& now,
                 const std::vector<config::proxy_settings>& proxies = {}) {
    pages.light(page);
    if (page == 1) {
      detail.template emplace<3>(this->actions, config::read_receipts_of(one), config::send_typing_of(one));
    } else if (page == 2) {
      detail.template emplace<4>(this->actions, proxies, config::proxy_of(one));
    } else {
      detail.template emplace<1>(this->actions, one);
      std::get<1>(detail).show(one, now);
    }
    this->begin_swap();
    this->invalidateLayout();
  }
  [[nodiscard]] account_privacy<Actions>* privacy() {
    return std::visit(overloaded{[](account_privacy<Actions>& one) { return &one; },
                                 [](auto&) -> account_privacy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] account_proxy<Actions>* proxy() {
    return std::visit(overloaded{[](account_proxy<Actions>& one) { return &one; },
                                 [](auto&) -> account_proxy<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] bool pages_open() const { return pages.visible(); }

  void select(const config::account_t& one, const model& now) {
    add.set_lit(false);
    selected = config::address_of(one);
    this->show_pages(true);
    this->show_page(0, one, now);
  }

  // Adding an account, beside the list.
  // The account list, or the chosen account's pages, down the side.
  void show_pages(bool shown) {
    pages.setVisible(shown);
    add.setVisible(!shown);
    list.setVisible(!shown);
    header.title.setText(shown && selected ? *selected : std::string("Accounts"));
    this->invalidateLayout();
  }
  // Back to the list of accounts, nothing chosen.
  void close_pages() {
    selected.reset();
    this->show_pages(false);
    detail.template emplace<0>("Choose an account.", 15.0f, dim_colour);
    this->begin_swap();
  }

  void show_adding() {
    this->show_pages(false);
    selected.reset();
    add.set_lit(true);
    detail.template emplace<2>(this->actions, proxies);
    this->begin_swap();
    this->invalidateLayout();
  }

  [[nodiscard]] account_editor<Actions>* editor() {
    return std::visit(overloaded{[](account_editor<Actions>& one) { return &one; },
                                 [](auto&) -> account_editor<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] add_account_pane<Actions>* adding() {
    return std::visit(overloaded{[](add_account_pane<Actions>& one) { return &one; },
                                 [](auto&) -> add_account_pane<Actions>* { return nullptr; }},
                      detail);
  }
  [[nodiscard]] xmpp_form<Actions>* xmpp() {
    return std::visit(overloaded{[](account_editor<Actions>& one) { return xmpp_form_in(one.form); },
                                 [](add_account_pane<Actions>& one) { return one.xmpp(); },
                                 [](auto&) -> xmpp_form<Actions>* { return nullptr; }},
                      detail);
  }

  void say(std::string text) {
    message.setText(std::move(text));
    this->invalidateLayout();
  }

};

// ---- the drawer -------------------------------------------------------------------

// The levels of motion, as the accounts file names them.
inline constexpr std::array<std::string_view, 3> kMotions{"full", "reduced", "none"};

// A level of motion chosen in the settings.
template <class Actions>
struct choose_motion {
  Actions* actions = nullptr;
  std::string_view level;
  void operator()() const { actions->set_motion(std::string(level)); }
};

// An account in the drawer: a round avatar with its initials, its address,
// and its protocol and state. A press makes it the current account, whose
// chats are the ones shown; the current one is lit and ticked.
template <class Actions>
struct drawer_account : nodes::Stack {
  Actions* actions = nullptr;
  std::string address;
  bool current = false;
  avatar_mark face;
  two_lines texts;
  // The account whose chats are shown: a tick at the end.
  icon_mark tick{icon::check{}};

  // Declared: the avatar, the address over its state, the tick.
  drawer_account(Actions* a, const config::account_t& saved, const model& now, bool is_current)
      : actions(a), address(config::address_of(saved)), current(is_current), face(address, address, 38.0f),
        texts(address, "", 14.0f, 3.0f) {
    this->setHorizontal();
    this->setGap(14.0f);
    fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 16.0f, 0.0f, 16.0f}});
    const auto [how, failed] = state_of(saved, now);
    texts.state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    texts.state.setColour(failed ? error_colour : dim_colour);
    tick.colour = accent_colour;
    tick.setVisible(current);
  }

  void forEachChild(auto&& f) {
    f(face);
    f(texts);
    f(tick);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    if (skia::SkFont* font = skiff::paint::defaultFont(); font && (current || fState.fHovered || this->showsFocus()))
      skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
  }

  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->switch_account(address);
    return true;
  }
  [[nodiscard]] scene::Semantics semantics() const {
    scene::Semantics out;
    out.fRole = scene::semantic_role::list_item{};
    out.fLabel = address;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// What the drawer holds, as Telegram's does: the accounts with Manage
// accounts under them, then Settings and Quit, each a full-width line with
// its icon.
template <class Actions>
struct drawer_panel : nodes::Stack {
  nodes::Text title{"mux", 20.0f, text_colour, true};
  std::vector<drawer_account<Actions>> accounts;
  row_item<ask<Actions, &Actions::open_accounts>> manage;
  nodes::Box<> rule_1{chosen_colour};
  row_item<ask<Actions, &Actions::open_settings>> settings;
  row_item<ask<Actions, &Actions::quit>> quit;

  explicit drawer_panel(Actions* a)
      : manage("Manage accounts", {a}, icon::person{}),
        settings("Settings", {a}, icon::gear{}),
        quit("Quit", {a}, icon::power{}) {
    title.apply({.margin = {18.0f, 20.0f, 14.0f, 20.0f}});
    manage.apply({.margin = {0.0f, 0.0f, 6.0f, 0.0f}});
    rule_1.apply({.margin = {0.0f, 0.0f, 6.0f, 0.0f}});
    fState.apply({.fill = true});
    rule_1.apply({.fillX = true, .height = 1.0f});
  }

  void forEachChild(auto&& f) {
    f(title);
    f(accounts);
    f(manage);
    f(rule_1);
    f(settings);
    f(quit);
  }

  void show(Actions* a, const std::vector<config::account_t>& saved, const model& now, std::string_view current) {
    accounts.clear();
    for (const config::account_t& one : saved)
      accounts.emplace_back(a, one, now, config::address_of(one) == current);
    this->invalidateLayout();
  }

};

// ---- the settings -------------------------------------------------------------------

// Settings, as Telegram Desktop shows them: a box over the window, a list of
// sections, and each section a page of the same box.
template <class Actions>
struct settings_home : nodes::Stack {
  page_header<ask<Actions, &Actions::close_settings>, ask<Actions, &Actions::close_settings>> header;
  row_item<ask<Actions, &Actions::open_accounts>> accounts;
  row_item<ask<Actions, &Actions::settings_animations>> animations;
  row_item<ask<Actions, &Actions::settings_proxies>> proxies;
  row_item<ask<Actions, &Actions::settings_appearance>> appearance;
  row_item<ask<Actions, &Actions::settings_rendering>> rendering;
  row_item<ask<Actions, &Actions::settings_storage>> storage;
  row_item<ask<Actions, &Actions::settings_files>> files;

  explicit settings_home(Actions* a)
      : header("Settings", {a}, {a}, false, true),
        accounts("Accounts", {a}, icon::person{}),
        animations("Animations", {a}, icon::motion{}),
        proxies("Proxies", {a}, icon::gear{}),
        appearance("Appearance", {a}, icon::eye{}),
        rendering("Rendering", {a}, icon::sliders{}), storage("Storage", {a}, icon::clip{}),
        files("Files", {a}, icon::send{}) {
    // Declared: the header, then the lines, one under another.
    fState.apply({.fill = true});
  }

  void forEachChild(auto&& f) {
    f(header);
    f(accounts);
    f(animations);
    f(appearance);
    f(rendering);
    f(storage);
    f(files);
    f(proxies);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};



template <class Actions>
struct animations_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  nodes::Text note{"How much the window moves. Reduced keeps the small movements, such as a section unfolding, "
                   "and shows panels at once.",
                   13.0f, dim_colour};
  row_item<choose_motion<Actions>> full;
  row_item<choose_motion<Actions>> reduced;
  row_item<choose_motion<Actions>> none;

  explicit animations_page(Actions* a)
      : header("Animations", {a}, {a}, true, true),
        full("Full", {a, kMotions[0]}, icon::none{}, false),
        reduced("Reduced", {a, kMotions[1]}, icon::none{}, false),
        none("None", {a, kMotions[2]}, icon::none{}, false) {
    note.apply({.fillX = true, .margin = {4.0f, 20.0f, 12.0f, 20.0f}});
    fState.apply({.fill = true});
    note.setWrapped(true);
  }

  void forEachChild(auto&& f) {
    f(header);
    f(note);
    f(full);
    f(reduced);
    f(none);
  }
  void show_receipts(bool) {}
  void show_motion(std::string_view level) {
    full.set_chosen(level == kMotions[0]);
    reduced.set_chosen(level == kMotions[1]);
    none.set_chosen(level == kMotions[2]);
  }
};

// A proxy profile opened from the list in Settings.
template <class Actions>
struct edit_proxy {
  Actions* actions = nullptr;
  int index = 0;
  void operator()() const { actions->edit_proxy(index); }
};
// A kind of proxy chosen on a profile's page.
template <class Actions>
struct choose_proxy_kind {
  Actions* actions = nullptr;
  config::proxy_kind_t kind;
  void operator()() const { actions->proxy_kind(kind); }
};

// Settings' Proxies page, as Gajim's Manage Proxies: the profiles, and a way
// to add one.
template <class Actions>
struct proxies_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  std::vector<row_item<edit_proxy<Actions>>> profiles;
  row_item<ask<Actions, &Actions::add_proxy>> add;
  nodes::Text empty{"No proxies yet. Accounts connect directly.", 13.0f, dim_colour};

  // With a way back to the settings' list where it was opened from there.
  proxies_page(Actions* a, const std::vector<config::proxy_settings>& all, bool with_back)
      : header("Proxies", {a}, {a}, with_back, true), add("Add proxy", {a}, icon::plus{}) {
    empty.setWrapped(true);
    empty.apply({.fillX = true, .margin = {8.0f, 20.0f, 0.0f, 20.0f}});
    fState.apply({.fill = true});
    for (std::size_t i = 0; i < all.size(); ++i)
      profiles.emplace_back(std::format("{} ({} {}:{})", all[i].name, config::label_of(config::proxy_kind_of(all[i].kind)),
                                        all[i].host, all[i].port),
                            edit_proxy<Actions>{a, static_cast<int>(i)}, icon::dot{proxy_colour(all[i].name)});
    empty.setVisible(all.empty());
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(profiles);
    f(add);
    f(empty);
  }
};

// SOCKS5 | HTTP: two segments in a frame, the chosen one lit by a plate
// that slides from one to the other.
template <class Actions>
struct kind_switch : nodes::Stack {
  segment<choose_proxy_kind<Actions>> socks;
  segment<choose_proxy_kind<Actions>> http;
  skiff::paint::Tween slide{0.0f, 180.0f, skiff::paint::movement::subtle{}};

  explicit kind_switch(Actions* a)
      : socks("SOCKS5", {a, config::proxy_kind::socks5{}}), http("HTTP", {a, config::proxy_kind::http{}}) {
    this->setHorizontal();
    this->setGap(1.0f);
    fState.apply({.autoSize = scene::axes::kBoth, .padding = {1.0f, 1.0f, 1.0f, 1.0f}});
  }
  void show(const config::proxy_kind_t& kind, bool at_once) {
    const float to = std::visit(overloaded{[](config::proxy_kind::socks5) { return 0.0f; },
                                           [](config::proxy_kind::http) { return 1.0f; }},
                                kind);
    if (at_once)
      slide.jump(to);
    else
      slide.setTarget(to);
    this->markDamaged();
  }
  void forEachChild(auto&& f) {
    f(socks);
    f(http);
  }
  [[nodiscard]] bool settling() const { return slide.moving(); }
  void update(double now_ms) {
    if (slide.step(now_ms))
      this->markDamaged();
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fState.fBounds, 0.0f, chosen_colour, alpha);
    const skia::SkRect& a = socks.bounds();
    const skia::SkRect& b = http.bounds();
    const float x = a.fLeft + (b.fLeft - a.fLeft) * slide.value();
    p.fillRounded(skia::SkRect::MakeXYWH(x, a.fTop, a.width(), a.height()), 0.0f, accent_colour, alpha);
  }
};


// One proxy profile's page: its name, SOCKS5 or HTTP, where, and who to be
// there; saved or deleted with its buttons. Declared: a column of these,
// nothing placed by hand.
template <class Actions>
struct proxy_editor : nodes::Stack {
  int index = -1;  // in the list; -1 for a new one
  config::proxy_kind_t kind = config::proxy_kind::socks5{};
  page_header<ask<Actions, &Actions::settings_proxies>, ask<Actions, &Actions::close_settings>> header;
  field name{"Name", "Home, Tor, Work…"};
  kind_switch<Actions> kinds;
  field host{"Host", "proxy.example.com"};
  field port{"Port", "1080"};
  field username{"User name", "none"};
  field password{"Password", "none"};
  nodes::Text message{"", 13.0f, dim_colour};
  button_row<widgets::Button<ask<Actions, &Actions::save_proxy_profile>>,
             widgets::Button<ask<Actions, &Actions::delete_proxy_profile>>>
      buttons;

  proxy_editor(Actions* a, const std::optional<config::proxy_settings>& from, int at)
      : index(at), header(from ? from->name : std::string("New proxy"), {a}, {a}, true, true), kinds(a),
        buttons(widgets::Button<ask<Actions, &Actions::save_proxy_profile>>("Save", {a}),
                widgets::Button<ask<Actions, &Actions::delete_proxy_profile>>("Delete", {a})) {
    fState.apply({.fill = true});
    this->setGap(8.0f);
    const auto inset = scene::Margin::horizontal(16.0f);
    for (field* one : {&name, &host, &port, &username, &password})
      one->apply({.margin = inset});
    kinds.apply({.margin = {4.0f, 0.0f, 4.0f, 16.0f}});
    message.setWrapped(true);
    message.apply({.fillX = true, .margin = inset});
    buttons.apply({.margin = inset});
    auto& [save, remove] = buttons.buttons;
    save.setPrimary(true);
    save.apply({.width = 110.0f, .height = 34.0f});
    remove.apply({.width = 110.0f, .height = 34.0f});
    remove.setVisible(from.has_value());
    password.box.setMasked(true);
    if (from) {
      name.box.setText(from->name);
      host.box.setText(from->host);
      port.box.setText(std::to_string(from->port));
      username.box.setText(from->username.value_or(""));
      password.box.setText(from->password.value_or(""));
    }
    kind = from ? config::proxy_kind_of(from->kind) : config::proxy_kind_t{config::proxy_kind::socks5{}};
    kinds.show(kind, true);
  }

  void set_kind(const config::proxy_kind_t& to) {
    kind = to;
    kinds.show(kind, false);
  }

  // The profile as typed, or what is wrong with it.
  [[nodiscard]] std::expected<config::proxy_settings, std::string> proxy() const {
    config::proxy_settings out{.name = name.box.text(), .kind = config::word_of(kind), .host = host.box.text()};
    if (out.name.empty())
      return std::unexpected("Name the proxy");
    if (out.host.empty())
      return std::unexpected("Type the proxy's host");
    const std::string& text = port.box.text();
    std::int64_t number = 0;
    const auto [last, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || failed != std::errc{} || last != text.data() + text.size() || number < 1 || number > 65535)
      return std::unexpected("A port is a number from 1 to 65535");
    out.port = number;
    out.username = typed_or_nothing(username.box.text());
    out.password = typed_or_nothing(password.box.text());
    return out;
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}

  void forEachChild(auto&& f) {
    f(header);
    f(name);
    f(kinds);
    f(host);
    f(port);
    f(username);
    f(password);
    f(message);
    f(buttons);
  }
};


// A theme chosen on the Appearance page, a renderer on the Rendering page.
template <class Actions>
struct choose_theme {
  Actions* actions = nullptr;
  config::theme_t theme;
  void operator()() const { actions->set_theme(theme); }
};
template <class Actions>
struct choose_renderer {
  Actions* actions = nullptr;
  config::renderer_t renderer;
  void operator()() const { actions->set_renderer(renderer); }
};

// A theme's card on the Appearance page: a small picture of it -- its
// background, a bubble of each side -- its name, and a ring where it is the
// one in use. Its colours are its own, not the theme up.
template <class Actions>
struct theme_card : nodes::Stack {
  Actions* actions = nullptr;
  config::theme_t theme;
  bool chosen = false;
  skia::SkColor back, bubble, mine;
  nodes::Text name;
  theme_card(Actions* a, config::theme_t which, std::string label, skia::SkColor b, skia::SkColor in, skia::SkColor out)
      : actions(a), theme(which), back(b), bubble(in), mine(out), name(std::move(label), 12.0f, dim_colour) {
    fState.apply({.width = 92.0f, .height = 92.0f, .padding = {66.0f, 0.0f, 0.0f, 0.0f}});
    name.apply({.alignSelf = scene::align::kMiddle});
  }
  void forEachChild(auto&& f) { f(name); }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    const skia::SkRect picture = skia::SkRect::MakeXYWH(box.fLeft + 6.0f, box.fTop + 4.0f, box.width() - 12.0f, 56.0f);
    p.fillRounded(picture, 8.0f, back, alpha);
    p.fillRounded(skia::SkRect::MakeXYWH(picture.fLeft + 6.0f, picture.fTop + 8.0f, 44.0f, 14.0f), 7.0f, bubble, alpha);
    p.fillRounded(skia::SkRect::MakeXYWH(picture.fRight - 50.0f, picture.fTop + 30.0f, 44.0f, 14.0f), 7.0f, mine, alpha);
    if (chosen)
      p.strokeRounded(skia::SkRect::MakeLTRB(picture.fLeft - 3.0f, picture.fTop - 3.0f, picture.fRight + 3.0f,
                                             picture.fBottom + 3.0f),
                      10.0f, accent_colour, 2.0f, alpha);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->set_theme(theme);
    return true;
  }
};

// An accent's circle: its colour, a ring where it is the one in use.
template <class Actions>
struct accent_circle : scene::Node {
  Actions* actions = nullptr;
  config::accent_t accent;
  bool chosen = false;
  // Its shade in the theme in use.
  skia::SkColor shade;
  accent_circle(Actions* a, config::accent_t which, const config::theme_t& in)
      : actions(a), accent(which), shade(colour_of(which, in)) {
    fState.apply({.width = 34.0f, .height = 34.0f});
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    const skia::SkRect dot = skia::SkRect::MakeXYWH(box.fLeft + 5.0f, box.fTop + 5.0f, 24.0f, 24.0f);
    p.fillRounded(dot, 12.0f, shade, alpha);
    if (chosen)
      p.strokeRounded(skia::SkRect::MakeLTRB(box.fLeft + 1.0f, box.fTop + 1.0f, box.fRight - 1.0f, box.fBottom - 1.0f), 16.0f,
                      shade, 2.0f, alpha);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    actions->set_accent(accent);
    return true;
  }
};

// Settings' Appearance page, as Telegram's: the themes as cards, and the
// accents as circles. Either changes at once.
template <class Actions>
struct appearance_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  nodes::Text theme_title = section_title("THEME");
  struct cards_row : nodes::Stack {
    // Telegram's cards, in its order, with its own pictures' colours:
    // the wallpaper, a bubble received, one sent.
    theme_card<Actions> classic, day, tinted, night;
    explicit cards_row(Actions* a)
        : classic(a, config::theme::classic{}, "Classic", skia::colorSetARGB(255, 155, 212, 148),
                  skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 234, 255, 220)),
          day(a, config::theme::day{}, "Day", skia::colorSetARGB(255, 126, 196, 234),
              skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 215, 240, 255)),
          tinted(a, config::theme::tinted{}, "Tinted", skia::colorSetARGB(255, 72, 87, 97),
                 skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 92, 167, 212)),
          night(a, config::theme::night{}, "Night", skia::colorSetARGB(255, 72, 87, 97),
                skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 117, 191, 181)) {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}});
    }
    void forEachChild(auto&& f) {
      f(classic);
      f(day);
      f(tinted);
      f(night);
    }
  } cards;
  nodes::Text accent_title = section_title("ACCENT");
  struct circles_row : nodes::Stack {
    std::vector<accent_circle<Actions>> circles;
    // The theme's own first, then Telegram's eight.
    circles_row(Actions* a, const config::theme_t& in) {
      this->setHorizontal();
      this->setGap(4.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}});
      for (const config::accent_t& one :
           {config::accent_t{config::accent::theme_own{}}, config::accent_t{config::accent::blue{}},
            config::accent_t{config::accent::green{}}, config::accent_t{config::accent::pink{}},
            config::accent_t{config::accent::orange{}}, config::accent_t{config::accent::purple{}},
            config::accent_t{config::accent::red{}}, config::accent_t{config::accent::grey{}},
            config::accent_t{config::accent::gold{}}})
        circles.emplace_back(a, one, in);
    }
    void forEachChild(auto&& f) { f(circles); }
  } circles;

  appearance_page(Actions* a, const config::theme_t& theme, const config::accent_t& accent)
      : header("Appearance", {a}, {a}, true, true), cards(a), circles(a, theme) {
    fState.apply({.fill = true});
    theme_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    accent_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    this->show(theme, accent);
  }
  void show(const config::theme_t& theme, const config::accent_t& accent) {
    for (auto* card : {&cards.classic, &cards.day, &cards.tinted, &cards.night})
      card->chosen = card->theme == theme;
    for (auto& circle : circles.circles)
      circle.chosen = circle.accent == accent;
    this->markDamaged();
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(theme_title);
    f(cards);
    f(accent_title);
    f(circles);
  }
};

// Settings' Rendering page: what draws the window, from the next start.
template <class Actions>
struct rendering_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  row_item<choose_renderer<Actions>> gpu;
  row_item<choose_renderer<Actions>> cpu;
  nodes::Text note{"Takes effect when mux starts again.", 13.0f, dim_colour};

  rendering_page(Actions* a, const config::renderer_t& renderer)
      : header("Rendering", {a}, {a}, true, true),
        gpu("OpenGL (the graphics card)", {a, config::renderer::opengl{}}, icon::none{}, false),
        cpu("Software (the processor)", {a, config::renderer::software{}}, icon::none{}, false) {
    note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    fState.apply({.fill = true});
    note.setWrapped(true);
    this->show(renderer);
  }
  void show(const config::renderer_t& renderer) {
    gpu.set_chosen(renderer == config::renderer_t{config::renderer::opengl{}});
    cpu.set_chosen(renderer == config::renderer_t{config::renderer::software{}});
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(gpu);
    f(cpu);
    f(note);
  }
};

// A limit changed by a step: halved or doubled.
template <class Actions>
struct step_limit {
  Actions* actions = nullptr;
  config::limit_t which;
  bool more = true;
  void operator()() const { actions->change_limit(which, more); }
};

// Settings' Storage page: how much is kept in memory and on disk, each a
// line with its number and a step down and up; and a way to clear what is
// kept on disk.
template <class Actions>
struct storage_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  struct stepper : nodes::Stack {
    nodes::Text label;
    nodes::Text value{"", 14.0f, accent_colour, true};
    icon_button<step_limit<Actions>> less;
    icon_button<step_limit<Actions>> more;
    stepper(Actions* a, std::string what, config::limit_t which)
        : label(std::move(what), 15.0f, text_colour), less(icon::minus{}, {a, which, false}),
          more(icon::plus{}, {a, which, true}) {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .height = 50.0f, .padding = {0.0f, 12.0f, 0.0f, 20.0f}});
      label.setElided(true);
      label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
      for (scene::Node* middle : std::initializer_list<scene::Node*>{&value, &less, &more})
        middle->apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(label);
      f(value);
      f(less);
      f(more);
    }
  };
  nodes::Text memory_title = section_title("IN MEMORY");
  stepper messages_in_memory;
  stepper pictures_in_memory;
  nodes::Text disk_title = section_title("ON DISK");
  stepper messages_on_disk;
  stepper pictures_on_disk;
  row_item<ask<Actions, &Actions::clear_stored>> clear;
  nodes::Text note{"Memory holds the newest of the chats read lately; the disk holds the rest, and what is scrolled "
                   "back to comes from there before the server. Past a limit, what was used longest ago goes first.",
                   13.0f, dim_colour};

  storage_page(Actions* a, const config::cache_limits& limits)
      : header("Storage", {a}, {a}, true, true),
        messages_in_memory(a, "Messages", config::limit::messages_in_memory{}),
        pictures_in_memory(a, "Pictures", config::limit::pictures_in_memory{}),
        messages_on_disk(a, "Messages", config::limit::messages_on_disk{}),
        pictures_on_disk(a, "Pictures", config::limit::pictures_on_disk{}),
        clear("Clear stored messages and pictures", {a}, icon::close{}) {
    fState.apply({.fill = true});
    memory_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    disk_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
    note.setWrapped(true);
    note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    this->show(limits);
  }
  void show(const config::cache_limits& limits) {
    messages_in_memory.value.setText(std::format("{} messages", limits.messages_in_memory));
    pictures_in_memory.value.setText(std::format("{} MB", limits.pictures_in_memory_mb));
    messages_on_disk.value.setText(std::format("{} MB", limits.messages_on_disk_mb));
    pictures_on_disk.value.setText(std::format("{} MB", limits.pictures_on_disk_mb));
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(memory_title);
    f(messages_in_memory);
    f(pictures_in_memory);
    f(disk_title);
    f(messages_on_disk);
    f(pictures_on_disk);
    f(clear);
    f(note);
  }
};

// Settings' Files page: what is done to a picture dropped on the window
// before it is sent.
template <class Actions>
struct files_page : nodes::Stack {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  nodes::Text title = section_title("PICTURES DROPPED ON THE WINDOW");
  switch_row<ask<Actions, &Actions::flip_strip_metadata>> strip;
  switch_row<ask<Actions, &Actions::flip_rename_pictures>> rename;
  nodes::Text note{"Metadata is where and when a picture was taken, with what, by whom: EXIF, XMP and the like. "
                   "It is cut out of the file; the picture itself is sent as it is, not compressed again.",
                   13.0f, dim_colour};
  files_page(Actions* a, const config::sending_settings& now)
      : header("Files", {a}, {a}, true, true), strip("Remove metadata", {a}), rename("Name them image.<type>", {a}) {
    fState.apply({.fill = true});
    title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    note.setWrapped(true);
    note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
    this->show(now);
  }
  void show(const config::sending_settings& now) {
    strip.toggle.setOnNow(now.strip_metadata);
    rename.toggle.setOnNow(now.rename);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(title);
    f(strip);
    f(rename);
    f(note);
  }
};

template <class Actions>
struct settings_dialog : scene::Node {
  Actions* actions = nullptr;
  std::string motion;
  std::variant<settings_home<Actions>, animations_page<Actions>, proxies_page<Actions>, proxy_editor<Actions>,
               appearance_page<Actions>, rendering_page<Actions>, storage_page<Actions>, files_page<Actions>>
      page;
  // What is up coming in from the side, fading in, when the page changes.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  float swap_from = 1.0f;
  void begin_swap(float side) {
    swap_from = side;
    swap.jump(0.0f);
    swap.setTarget(1.0f);
    this->invalidateLayout();
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->invalidateLayout();
  }

  settings_dialog(Actions* a, std::string level) : actions(a), motion(std::move(level)), page(std::in_place_index<0>, a) {
    fState.apply({.fill = true});
  }

  void forEachChild(auto&& f) { f(page); }

  // Home comes back from the left, the pages come in from the right.
  void show_home() {
    page.template emplace<0>(actions);
    this->begin_swap(-1.0f);
  }
  void show_animations() {
    this->begin_swap(1.0f);
    page.template emplace<1>(actions);
    this->show_motion(motion);
  }
  void show_appearance(const config::theme_t& theme, const config::accent_t& accent) {
    page.template emplace<4>(actions, theme, accent);
    this->begin_swap(1.0f);
  }
  void show_rendering(const config::renderer_t& renderer) {
    page.template emplace<5>(actions, renderer);
    this->begin_swap(1.0f);
  }
  void show_files(const config::sending_settings& now) {
    page.template emplace<7>(actions, now);
    this->begin_swap(1.0f);
  }
  void show_storage(const config::cache_limits& limits) {
    page.template emplace<6>(actions, limits);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] storage_page<Actions>* storage() {
    return std::visit(overloaded{[](storage_page<Actions>& one) { return &one; },
                                 [](auto&) -> storage_page<Actions>* { return nullptr; }},
                      page);
  }
  [[nodiscard]] rendering_page<Actions>* rendering() {
    return std::visit(overloaded{[](rendering_page<Actions>& one) { return &one; },
                                 [](auto&) -> rendering_page<Actions>* { return nullptr; }},
                      page);
  }
  [[nodiscard]] appearance_page<Actions>* appearance() {
    return std::visit(overloaded{[](appearance_page<Actions>& one) { return &one; },
                                 [](auto&) -> appearance_page<Actions>* { return nullptr; }},
                      page);
  }
  void show_proxies(const std::vector<config::proxy_settings>& all, bool with_back = true) {
    page.template emplace<2>(actions, all, with_back);
    this->begin_swap(1.0f);
  }
  void show_proxy(const std::optional<config::proxy_settings>& from, int index) {
    page.template emplace<3>(actions, from, index);
    this->begin_swap(1.0f);
  }
  [[nodiscard]] proxy_editor<Actions>* editor() {
    return std::visit(overloaded{[](proxy_editor<Actions>& one) { return &one; },
                                 [](auto&) -> proxy_editor<Actions>* { return nullptr; }},
                      page);
  }
  void show_motion(std::string level) {
    motion = std::move(level);
    std::visit([this](auto& one) { one.show_motion(motion); }, page);
  }


  void layoutChildren() {
    std::visit(
        [this](auto& one) {
          one.fState.arrange(0.0f, 0.0f);
          const float value = swap.value();
          const skia::SkRect box = fState.contentBox();
          one.fState.setAlpha(value);
          scene::layout(one, skia::SkRect::MakeXYWH(box.fLeft + (1.0f - value) * 32.0f * swap_from, box.fTop,
                                                    box.width(), box.height()));
        },
        page);
  }
};

// What is done with a message from its menu, as the program keeps it.
template <class Actions>
struct context_menu : scene::Node {
  struct card : nodes::Stack {
    row_item<ask<Actions, &Actions::menu_reply>> reply;
    row_item<ask<Actions, &Actions::menu_edit>> edit;
    row_item<not_yet<Actions>> pin;
    row_item<ask<Actions, &Actions::menu_copy>> copy;
    row_item<ask<Actions, &Actions::menu_copy_link>> copy_link;
    row_item<ask<Actions, &Actions::menu_save>> save;
    row_item<not_yet<Actions>> forward;
    row_item<ask<Actions, &Actions::menu_delete>> remove;
    // Who has seen it, as Telegram's menu says at its top: how many, and
    // their names under it.
    row_item<nothing> seen;
    std::vector<nodes::Text> seen_names;
    nodes::Box<> seen_band{band_colour};
    // As tdesktop's, in its order, what does not apply to the message left
    // out: Reply, Edit, Pin, Copy, Copy Message Link, Save As, Forward,
    // Delete; and who has seen it, at the foot.
    card(Actions* a, const menu_facts& facts)
        : reply("Reply", {a}, icon::back{}), edit("Edit", {a}, icon::sliders{}),
          pin("Pin", {a, "Pinning messages"}, icon::check{}),
          copy(facts.selection ? "Copy Selected Text" : "Copy Text", {a}, icon::clip{}),
          copy_link("Copy Message Link", {a}, icon::info{}), save("Save As…", {a}, icon::send{}),
          forward("Forward", {a, "Forwarding"}, icon::send{}), remove("Delete", {a}, icon::close{}),
          seen(facts.seen.empty() ? std::string("Not seen yet") : std::format("Seen by {}", facts.seen.size()), {},
               icon::check{}) {
      const std::vector<std::string>& readers = facts.seen;
      edit.setVisible(facts.own && !facts.text.empty() && !facts.media);
      copy.setVisible(!facts.copied.empty());
      copy_link.setVisible(!facts.link.empty());
      save.setVisible(facts.media.has_value());
      remove.setVisible(facts.own);
      for (std::size_t i = 0; i < readers.size() && i < 10; ++i) {
        seen_names.emplace_back(readers[i], 13.0f, dim_colour);
        seen_names.back().setElided(true);
        seen_names.back().apply({.fillX = true, .margin = {0.0f, 16.0f, 2.0f, 64.0f}});
      }
      if (readers.size() > 10) {
        seen_names.emplace_back(std::format("and {} more", readers.size() - 10), 13.0f, dim_colour);
        seen_names.back().apply({.fillX = true, .margin = {0.0f, 16.0f, 2.0f, 64.0f}});
      }
      seen_band.apply({.fillX = true, .height = 1.0f, .margin = {4.0f, 0.0f, 4.0f, 0.0f}});
      fState.apply({.width = 230.0f, .autoSize = scene::axes::kY, .padding = {6.0f, 0.0f, 6.0f, 0.0f}});
    }
    void forEachChild(auto&& f) {
      f(reply);
      f(edit);
      f(pin);
      f(copy);
      f(copy_link);
      f(save);
      f(forward);
      f(remove);
      f(seen_band);
      f(seen);
      f(seen_names);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      skia::SkFont* font = skiff::paint::defaultFont();
      if (font == nullptr)
        return;
      const skiff::paint::Painter p(canvas, *font);
      const skia::SkRect& box = fState.fBounds;
      p.fillRounded(skia::SkRect::MakeLTRB(box.fLeft, box.fTop + 3.0f, box.fRight, box.fBottom + 3.0f), 10.0f,
                    skia::colorSetARGB(70, 0, 0, 0), alpha);
      p.fillRounded(box, 10.0f, sidebar_colour, alpha);
      p.strokeRounded(box, 10.0f, band_colour, 1.0f, alpha);
    }
  } menu;
  Actions* actions = nullptr;

  explicit context_menu(Actions* a, const menu_facts& facts) : menu(a, facts), actions(a) {
    const float x = facts.x, y = facts.y;
    fState.apply({.fill = true});
    menu.apply({.x = x, .y = y});
  }
  void forEachChild(auto&& f) { f(menu); }
  // A press off the menu closes it.
  [[nodiscard]] bool acceptsInput() const { return true; }
  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
    actions->close_menu();
    reply.handle();
  }
};

// What is about to be sent, as tdesktop's send box shows it: its title, a
// picture scaled to the box or a file's row for each, a caption, and Cancel
// and Send.
struct pending_file {
  std::string name;
  std::string key;  // its picture's, where it is one: "thumb:local:..."
  std::int64_t size = 0;
  bool image = false;
};
template <class Actions>
struct send_box : nodes::Stack {
  nodes::Text title;
  struct previews_column : nodes::Stack {
    struct picture_preview : scene::Node {
      std::string key;
      picture_preview(std::string k, float width, float height) : key(std::move(k)) {
        fState.apply({.width = width, .height = height, .alignSelf = scene::align::kMiddle});
      }
      void drawSelf(skia::SkCanvas* canvas, float alpha) {
        const skia::SkRect& box = fState.fBounds;
        const int saved = canvas->save();
        canvas->clipRRect(skia::SkRRect::MakeRectXY(box, 10.0f, 10.0f), true);
        if (const skia::Sp<skia::SkImage>* image = avatar_images().find(key); image && *image) {
          skia::SkPaint paint;
          paint.setAlphaf(alpha);
          canvas->drawImageRect(*image, box, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
        }
        canvas->restoreToCount(saved);
      }
    };
    std::vector<picture_preview> pictures;
    std::vector<file_view> files;
    explicit previews_column(const std::vector<pending_file>& all) {
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      for (const pending_file& one : all) {
        if (one.image) {
          const skia::Sp<skia::SkImage>* image = avatar_images().find(one.key);
          float w = image && *image ? static_cast<float>((*image)->width()) : 380.0f;
          float h = image && *image ? static_cast<float>((*image)->height()) : 240.0f;
          const float scale = std::min({1.0f, 380.0f / w, (all.size() > 1 ? 160.0f : 300.0f) / h});
          pictures.emplace_back(one.key, w * scale, h * scale);
        } else {
          files.emplace_back(std::string(), one.name, one.size);
        }
      }
    }
    void forEachChild(auto&& f) {
      f(pictures);
      f(files);
    }
  };
  nodes::ScrollContainer<previews_column> previews;
  widgets::TextArea<> caption{"Add a caption…"};
  struct buttons_row : nodes::Stack {
    widgets::Button<ask<Actions, &Actions::close_send_box>> cancel;
    widgets::Button<ask<Actions, &Actions::send_files>> send;
    explicit buttons_row(Actions* a) : cancel("Cancel", {a}), send("Send", {a}) {
      this->setHorizontal();
      this->setGap(8.0f);
      fStack.justify = nodes::justify::end{};
      fState.apply({.fillX = true, .autoSize = scene::axes::kY});
      send.setPrimary(true);
      cancel.apply({.width = 96.0f, .height = 36.0f});
      send.apply({.width = 96.0f, .height = 36.0f});
    }
    void forEachChild(auto&& f) {
      f(cancel);
      f(send);
    }
  } buttons;

  send_box(Actions* a, const std::vector<pending_file>& all)
      : title(all.size() == 1 ? (all.front().image ? "Send a photo" : "Send a file")
                              : std::format("Send {} {}", all.size(),
                                            std::ranges::all_of(all, &pending_file::image) ? "photos" : "files"),
              17.0f, text_colour, true),
        previews(previews_column(all)), buttons(a) {
    this->setGap(12.0f);
    fState.apply({.fill = true, .padding = {18.0f, 20.0f, 16.0f, 20.0f}});
    previews.apply({.fillX = true, .grow = scene::axes::kY});
    caption.apply({.fillX = true});
  }
  void forEachChild(auto&& f) {
    f(title);
    f(previews);
    f(caption);
    f(buttons);
  }
};

// A picture seen whole, as tdesktop's media viewer -- but over this window,
// not in one of its own: dark behind; at the top, who sent it and when, and
// the buttons -- zoom out and in, save, close; the picture fitted in the
// rest, zoomed by the wheel or the buttons, dragged about when larger than
// the room. A press on the dark around it closes it; the whole picture
// replaces its thumbnail when it has come.
template <class Actions>
struct picture_viewer : nodes::Stack {
  Actions* actions = nullptr;
  std::string source;
  // The viewer's own buttons act on it; the rest on the program.
  struct zoom_by {
    picture_viewer* viewer;
    float factor;
    void operator()() const { viewer->zoom_to(viewer->zoom * factor); }
  };
  struct save_it {
    Actions* actions;
    std::string source;
    void operator()() const { actions->save_picture(source); }
  };
  struct top_bar : nodes::Stack {
    avatar_mark face;
    two_lines texts;
    nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
    icon_button<zoom_by> smaller;
    icon_button<zoom_by> larger;
    icon_button<save_it> save;
    icon_button<ask<Actions, &Actions::close_picture>> close;
    top_bar(Actions* a, picture_viewer* viewer, const std::string& source, const std::string& sender,
            const std::string& name, const std::string& when)
        : face(sender, name, 36.0f), texts(name, when, 14.0f, 2.0f), smaller(icon::minus{}, {viewer, 1.0f / 1.25f}),
          larger(icon::plus{}, {viewer, 1.25f}), save(icon::send{}, {a, source}), close(icon::close{}, {a}) {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 56.0f, .padding = {0.0f, 12.0f, 0.0f, 16.0f}});
      texts.name.setColour(skia::colorSetARGB(255, 255, 255, 255));
      texts.state.setColour(skia::colorSetARGB(255, 200, 200, 200));
      gap.apply({.height = 1.0f, .grow = scene::axes::kX});
      for (auto* button : {&smaller.colour, &larger.colour, &close.colour})
        *button = skia::colorSetARGB(255, 255, 255, 255);
      save.colour = skia::colorSetARGB(255, 255, 255, 255);
      for (scene::Node* middle : std::initializer_list<scene::Node*>{&smaller, &larger, &save, &close})
        middle->apply({.alignSelf = scene::align::kMiddle});
    }
    void forEachChild(auto&& f) {
      f(face);
      f(texts);
      f(gap);
      f(smaller);
      f(larger);
      f(save);
      f(close);
    }
  } top;
  // Where the picture is drawn: fitted, zoomed, moved.
  struct stage : scene::Node {
    picture_viewer* viewer;
    explicit stage(picture_viewer* v) : viewer(v) { fState.apply({.fillX = true, .grow = scene::axes::kY}); }
    [[nodiscard]] const skia::Sp<skia::SkImage>* image() const {
      const skia::Sp<skia::SkImage>* one = avatar_images().find("full:" + viewer->source);
      if (!one || !*one)
        one = avatar_images().find("thumb:" + viewer->source);
      return one && *one ? one : nullptr;
    }
    [[nodiscard]] skia::SkRect where() const {
      const skia::Sp<skia::SkImage>* one = this->image();
      const skia::SkRect& box = fState.fBounds;
      if (!one)
        return skia::SkRect::MakeEmpty();
      const float w = static_cast<float>((*one)->width()), h = static_cast<float>((*one)->height());
      const float fit = std::min({1.0f, (box.width() - 48.0f) / w, (box.height() - 48.0f) / h});
      const float scale = fit * viewer->zoom;
      return skia::SkRect::MakeXYWH(box.centerX() - w * scale * 0.5f + viewer->pan_x,
                                    box.centerY() - h * scale * 0.5f + viewer->pan_y, w * scale, h * scale);
    }
    void drawSelf(skia::SkCanvas* canvas, float alpha) {
      const skia::Sp<skia::SkImage>* one = this->image();
      if (!one)
        return;
      const int saved = canvas->save();
      canvas->clipRect(fState.fBounds);
      skia::SkPaint paint;
      paint.setAlphaf(alpha);
      canvas->drawImageRect(*one, this->where(), skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
      canvas->restoreToCount(saved);
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool onScroll(float ticks) {
      viewer->zoom_to(viewer->zoom * std::pow(1.25f, ticks));
      return true;
    }
    bool dragging = false;
    float last_x = 0.0f, last_y = 0.0f;
    using Node::onPointer;
    void onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
      // Off the picture: closed. On it, and larger than the room: dragged.
      if (!this->where().contains(press.x, press.y)) {
        viewer->actions->close_picture();
        reply.handle();
        return;
      }
      dragging = true;
      last_x = press.x;
      last_y = press.y;
      reply.capturePointer();
      reply.handle();
    }
    void onPointer(scene::phase::target, const scene::pointer::move& at, scene::PointerReply& reply) {
      if (!dragging || viewer->zoom <= 1.0f)
        return;
      viewer->pan_x += at.x - last_x;
      viewer->pan_y += at.y - last_y;
      last_x = at.x;
      last_y = at.y;
      this->markDamaged();
      reply.handle();
    }
    void onPointer(scene::phase::target, const scene::pointer::up&, scene::PointerReply& reply) {
      if (dragging) {
        dragging = false;
        reply.releasePointer();
      }
    }
  } view;
  float zoom = 1.0f;
  float pan_x = 0.0f, pan_y = 0.0f;
  void zoom_to(float wanted) {
    zoom = std::clamp(wanted, 1.0f, 8.0f);
    if (zoom == 1.0f)
      pan_x = pan_y = 0.0f;
    view.markDamaged();
  }

  picture_viewer(Actions* a, std::string where, std::string sender, std::string name, std::string when)
      : actions(a), source(std::move(where)), top(a, this, source, sender, name, when), view(this) {
    fState.apply({.fill = true});
  }
  void forEachChild(auto&& f) {
    f(top);
    f(view);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkPaint dim;
    dim.setColor(skia::colorSetARGB(0xe6, 0, 0, 0));
    dim.setAlphaf(dim.getAlphaf() * alpha);
    canvas->drawRect(fState.fBounds, dim);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

// ---- the window -------------------------------------------------------------------

// The conversations; over them the panel that is open, if one is, sliding in
// from the right and back out when closed; and over both, the drawer, pulled
// out from the left. All in this one window, switched by the program between
// events.
template <class Actions>
struct window : scene::Node {
  using panel_type = std::variant<accounts_panel<Actions>>;
  using with_drawer = widgets::Drawer<conversations_screen<Actions>, drawer_panel<Actions>>;

  // What the window holds, made anew when the theme changes: what is made
  // takes its colours then.
  struct parts {
    nodes::Box<> backdrop{background};
    // The pages slide over the drawer too: Manage accounts comes in over it.
    widgets::SlideOver<with_drawer, panel_type> frame;
    widgets::Dialog<settings_dialog<Actions>> settings;
    widgets::Dialog<notice_box<Actions>> notice;
    std::optional<context_menu<Actions>> menu;
    std::optional<picture_viewer<Actions>> viewer;
    widgets::Dialog<send_box<Actions>> sending;

    explicit parts(Actions* a) : frame(std::piecewise_construct, std::forward_as_tuple(a), std::forward_as_tuple(a)) {
      backdrop.apply({.fill = true});
      frame.setSheetColour(background);
      frame.base().setSheetColour(sidebar_colour);
      settings.setSheetColour(sidebar_colour);
      sending.setSheetColour(sidebar_colour);
      settings.setSize(440.0f, 520.0f);
      notice.setSheetColour(sidebar_colour);
      notice.setSize(440.0f, 240.0f);
    }
  };

  Actions* actions = nullptr;
  std::optional<parts> p;

  explicit window(Actions* a) : actions(a) {
    fState.apply({.fill = true});
    p.emplace(a);
  }
  // Everything made again, in the colours of the theme now in place.
  void rebuild() {
    p.reset();
    p.emplace(actions);
    this->invalidateLayout();
    this->markDamaged();
  }
  void forEachChild(auto&& f) {
    if (!p)
      return;
    f(p->backdrop);
    f(p->frame);
    f(p->settings);
    f(p->notice);
    f(p->sending);
    f(p->menu);
    f(p->viewer);
  }

  [[nodiscard]] conversations_screen<Actions>& main() { return p->frame.base().base(); }
  // The panel that is up, not on its way out.
  [[nodiscard]] panel_type* open_panel() { return p->frame.shown(); }

  // A panel opened, in place of the one up if there is one.
  // A panel up: the one on top if it is one of these, else a new one sliding
  // in over it.
  template <class Panel>
  Panel& open() {
    if (panel_type* up = p->frame.shown())
      if (Panel* same = std::get_if<Panel>(up))
        return *same;
    return std::get<Panel>(p->frame.open(std::in_place_type<Panel>, actions));
  }
  // The top panel goes, and the one under it is up again.
  void back_panel() { p->frame.back(); }
  void close() { p->frame.close(); }
  // From the program, between events.
  void drop_closed() {
    p->frame.dropClosed();
    p->settings.dropClosed();
    p->notice.dropClosed();
    p->sending.dropClosed();
  }

  void open_settings(std::string motion) { p->settings.open(actions, std::move(motion)); }
  void open_picture(std::string source, std::string sender, std::string name, std::string when) {
    p->viewer.emplace(actions, std::move(source), std::move(sender), std::move(name), std::move(when));
  }
  void open_send_box(const std::vector<pending_file>& files) {
    p->sending.setSize(440.0f, 560.0f);
    p->sending.open(actions, files);
  }
  void close_send_box() { p->sending.close(); }
  [[nodiscard]] send_box<Actions>* send_box_up() { return p->sending.shown(); }
  void close_picture() { p->viewer.reset(); }
  void close_settings() { p->settings.close(); }
  [[nodiscard]] settings_dialog<Actions>* settings_up() { return p->settings.shown(); }

  void open_drawer() { p->frame.base().open(); }
  void close_drawer() { p->frame.base().close(); }
  void close_drawer_now() { p->frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return p->frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return p->frame.settling(); }

  void open_menu(const menu_facts& facts) { p->menu.emplace(actions, facts); }
  void close_menu() { p->menu.reset(); }

  void show_notice(std::string what) {
    p->notice.open(actions, "Not implemented yet", std::format("{} isn't implemented yet.", what));
  }
  void show_message(std::string heading, std::string text) {
    p->notice.open(actions, std::move(heading), std::move(text));
  }
  void close_notice() { p->notice.close(); }

  void show(const std::vector<config::account_t>& saved, const model& now) {
    const auto& current = p->frame.base().base().current;
    p->frame.base().content().show(actions, saved, now, current ? std::string_view(current->address) : std::string_view());
  }
  void show_motion(std::string_view level) {
    if (auto* up = p->settings.shown())
      up->show_motion(std::string(level));
  }

  // Its layers, each filling the window, as the default layout places them:
  // nothing placed by hand.
};

}  // namespace mux::ui
