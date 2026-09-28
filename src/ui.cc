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
//   void accounts_back()              -- ← on the accounts page
//   void account_page(int)           -- a page of the chosen account
//   void flip_account_receipts(), choose_account_proxy(int), manage_proxies()
//   void settings_proxies(), add_proxy(), edit_proxy(int), proxy_kind(int),
//        save_proxy_profile(), delete_proxy_profile()
//   void settings_appearance(), set_theme(std::string), set_renderer(std::string)
//   void not_implemented(std::string what)  -- a box saying it is not there yet
//   void close_notice()
//   void resize_sidebar(float x)     -- the chat list's edge dragged to x
//   void resize_info(float x)        -- the chat info's edge dragged to x
//   void submit_message(std::string text)  -- Enter in the message field
//   void send_typed()                -- the send arrow: what is in the field
//   void toggle_info()               -- the chosen chat's info, beside it
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
}  // namespace icon
using icon_t = std::variant<icon::none, icon::person, icon::gear, icon::power, icon::plus, icon::motion, icon::back,
                            icon::close, icon::info, icon::people, icon::add_person, icon::bell, icon::sliders,
                            icon::leave, icon::check, icon::clip, icon::send, icon::eye>;

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

// Up to two letters for an avatar: the first of each of the first two words
// of a name, or of an address's local part.
[[nodiscard]] inline std::string initials_of(std::string_view name) {
  if (name.starts_with('@'))
    name.remove_prefix(1);
  name = name.substr(0, name.find_first_of("@:"));
  std::string out;
  bool start = true;
  for (const char c : name) {
    const bool letter = std::isalnum(static_cast<unsigned char>(c)) != 0 || (static_cast<unsigned char>(c) & 0x80) != 0;
    if (letter && start && out.size() < 2 && (static_cast<unsigned char>(c) & 0x80) == 0)
      out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    start = !letter;
  }
  return out.empty() ? std::string("?") : out;
}

// A round avatar: the colour of `id`, and the initials of `name` in it.
inline void draw_avatar(skia::SkCanvas* canvas, const skia::SkRect& disc, std::string_view id, std::string_view name,
                        float alpha) {
  skia::SkFont* font = skiff::paint::defaultFont();
  if (font == nullptr)
    return;
  const skiff::paint::Painter p(canvas, *font);
  p.fillRounded(disc, disc.width() * 0.5f, avatar_colour(id), alpha);
  const std::string letters = initials_of(name);
  const float size = disc.width() * 0.38f;
  const float width = p.measure(letters, size, true);
  p.textIn(disc, letters, size, text_colour, alpha, true, (disc.width() - width) * 0.5f);
}

// A line of a list or a menu, as wide as what holds it and square: an icon
// on the left, its text, and a radio mark on the right where it is one of a
// choice. It lights under the pointer; a press does `act`.
template <class Act>
struct row_item : scene::Node {
  Act act;
  icon_t icon;
  // Whether it is one of a choice, and the chosen one.
  std::optional<bool> radio;
  nodes::Text label;

  static constexpr float kHeight = 46.0f;

  row_item(std::string text, Act what, icon_t mark = icon::none{}, std::optional<bool> choice = std::nullopt)
      : act(std::move(what)), icon(mark), radio(choice), label(std::move(text), 15.0f, text_colour) {
    fState.apply({.fillX = true, .height = kHeight});
    label.setElided(true);
  }

  void set_chosen(bool on) {
    radio = on;
    this->markDamaged();
  }
  // Lit as the line whose page is shown beside the list.
  void set_lit(bool on) {
    lit = on;
    this->markDamaged();
  }
  bool lit = false;

  void forEachChild(auto&& f) { f(label); }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const float left = std::visit(overloaded{[](icon::none) { return 20.0f; }, [](auto) { return 64.0f; }}, icon);
    label.setMaxWidth(std::max(0.0f, box.width() - left - (radio ? 52.0f : 16.0f)));
    label.fState.arrange(left, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(label, box);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (lit || fState.fHovered || this->showsFocus())
      p.fillRounded(box, 0.0f, chosen_colour, alpha);
    draw_icon(canvas, icon, skia::SkRect::MakeXYWH(box.fLeft + 20.0f, box.fTop, 24.0f, box.height()), dim_colour, alpha);
    if (radio) {
      const float x = box.fRight - 30.0f, y = box.centerY();
      canvas->drawCircle(x, y, 8.0f, pen(*radio ? accent_colour : dim_colour, alpha, 2.0f));
      if (*radio)
        p.fillRounded(skia::SkRect::MakeLTRB(x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f), 4.0f, accent_colour, alpha);
    }
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
struct page_header : scene::Node {
  icon_button<Back> back;
  nodes::Text title;
  icon_button<Close> close;

  static constexpr float kHeight = 54.0f;

  page_header(std::string name, Back to, Close shut, bool has_back, bool has_close)
      : back(icon::back{}, std::move(to)), title(std::move(name), 17.0f, text_colour, true),
        close(icon::close{}, std::move(shut)) {
    fState.apply({.fillX = true, .height = kHeight});
    back.setVisible(has_back);
    close.setVisible(has_close);
    title.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(back);
    f(title);
    f(close);
  }
  void layoutChildren() {
    const skia::SkRect box = scene::inset(fState.contentBox(), 10.0f, 0.0f);
    back.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(back, box);
    close.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(close, box);
    const float left = back.visible() ? back.bounds().width() + 12.0f : 10.0f;
    title.setMaxWidth(std::max(0.0f, box.width() - left - 48.0f));
    title.fState.arrange(left, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(title, box);
  }
};

// One segment of a segmented control: square, its text centred, filled
// with the accent while it is the one chosen.
template <class Act>
struct segment : scene::Node {
  Act act;
  bool active = false;
  nodes::Text label;

  segment(std::string text, Act what) : act(std::move(what)), label(std::move(text), 13.0f, text_colour, true) {
    fState.apply({.width = 92.0f, .height = 28.0f});
  }

  void set_active(bool on) {
    active = on;
    label.setColour(on ? background : text_colour);
    this->markDamaged();
  }

  void forEachChild(auto&& f) { f(label); }
  void layoutChildren() {
    label.fState.arrange(0.0f, 0.0f, scene::anchor::kCentre, scene::anchor::kCentre);
    scene::layout(label, fState.contentBox());
  }
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
inline void use_theme(std::string_view name) {
  auto& widget = widgets::theme();
  widget = widgets::Theme{};
  background = skia::colorSetARGB(255, 24, 27, 30);
  sidebar_colour = skia::colorSetARGB(255, 32, 36, 40);
  chosen_colour = skia::colorSetARGB(255, 52, 60, 66);
  text_colour = skia::colorSetARGB(255, 235, 240, 243);
  dim_colour = skia::colorSetARGB(255, 150, 162, 170);
  accent_colour = skia::colorSetARGB(255, 102, 204, 255);
  error_colour = skia::colorSetARGB(255, 255, 120, 110);
  selected_colour = skia::colorSetARGB(255, 43, 82, 120);
  band_colour = skia::colorSetARGB(255, 18, 20, 23);
  section_colour = skia::colorSetARGB(255, 26, 29, 33);
  tile_colour = skia::colorSetARGB(255, 40, 45, 50);
  bubble_colour = skia::colorSetARGB(255, 33, 41, 52);
  sent_time_colour = skia::colorSetARGB(255, 170, 200, 230);
  if (name == "light") {
    background = skia::colorSetARGB(255, 241, 243, 245);
    sidebar_colour = skia::colorSetARGB(255, 255, 255, 255);
    chosen_colour = skia::colorSetARGB(255, 229, 233, 237);
    text_colour = skia::colorSetARGB(255, 22, 26, 30);
    dim_colour = skia::colorSetARGB(255, 108, 118, 128);
    accent_colour = skia::colorSetARGB(255, 36, 140, 220);
    error_colour = skia::colorSetARGB(255, 205, 60, 50);
    selected_colour = skia::colorSetARGB(255, 205, 228, 250);
    band_colour = skia::colorSetARGB(255, 222, 226, 230);
    section_colour = skia::colorSetARGB(255, 235, 238, 241);
    tile_colour = skia::colorSetARGB(255, 238, 241, 244);
    bubble_colour = skia::colorSetARGB(255, 255, 255, 255);
    sent_time_colour = skia::colorSetARGB(255, 80, 120, 160);
    widget.fSurface = skia::colorSetARGB(255, 233, 236, 240);
    widget.fSurfaceHover = skia::colorSetARGB(255, 223, 228, 233);
    widget.fSurfaceActive = skia::colorSetARGB(255, 212, 218, 224);
    widget.fText = skia::colorSetARGB(255, 22, 26, 30);
    widget.fLabel = skia::colorSetARGB(255, 40, 48, 56);
    widget.fTextDim = skia::colorSetARGB(255, 100, 110, 120);
    widget.fTextFaint = skia::colorSetARGB(255, 140, 150, 160);
    widget.fAccent = accent_colour;
    widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
  }
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
template <class Actions>
struct conversation_row : scene::Node {
  Actions* actions = nullptr;
  conversation_id id;
  bool chosen = false;
  std::int64_t unread = 0;
  nodes::Text name;
  nodes::Text time;
  nodes::Text preview;

  static constexpr float kHeight = 62.0f;
  static constexpr float kTextLeft = 68.0f;

  bool muted = false;

  conversation_row(Actions* a, const conversation& one, bool is_chosen, bool is_muted)
      : actions(a), id(one.id), chosen(is_chosen), unread(one.unread), muted(is_muted),
        name(display_name(one), 14.0f, text_colour, true), time("", 12.0f, is_chosen ? text_colour : dim_colour),
        preview("", 13.0f, is_chosen ? text_colour : dim_colour) {
    fState.apply({.fillX = true, .height = kHeight});
    name.setElided(true);
    preview.setElided(true);
    if (!one.timeline.empty()) {
      const message& last = one.timeline.back();
      time.setText(clock_of(last.at));
      std::string text = last.redacted ? "(removed)" : last.body.plain;
      std::ranges::replace(text, '\n', ' ');
      if (last.outgoing)
        text = "You: " + text;
      else if (is_group(one))
        text = sender_name(one, last.sender) + ": " + text;
      preview.setText(std::move(text));
    }
  }

  void forEachChild(auto&& f) {
    f(name);
    f(time);
    f(preview);
  }

  [[nodiscard]] float badge_width() const {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr || unread <= 0)
      return 0.0f;
    const skiff::paint::Painter p(nullptr, *font);
    return std::max(22.0f, p.measure(std::to_string(unread), 12.0f, true) + 14.0f);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    time.fState.arrange(-12.0f, 12.0f, scene::anchor::kTopRight, scene::anchor::kTopRight);
    scene::layout(time, box);
    name.setMaxWidth(std::max(0.0f, time.bounds().fLeft - box.fLeft - kTextLeft - 8.0f));
    name.fState.arrange(kTextLeft, 11.0f);
    scene::layout(name, box);
    const float badge = this->badge_width();
    preview.setMaxWidth(std::max(0.0f, box.width() - kTextLeft - 12.0f - (badge > 0.0f ? badge + 8.0f : 0.0f)));
    preview.fState.arrange(kTextLeft, 34.0f);
    scene::layout(preview, box);
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (chosen)
      p.fillRounded(box, 0.0f, selected_colour, alpha);
    else if (fState.fHovered || this->showsFocus())
      p.fillRounded(box, 0.0f, chosen_colour, alpha);
    draw_avatar(canvas, skia::SkRect::MakeXYWH(box.fLeft + 10.0f, box.centerY() - 23.0f, 46.0f, 46.0f), id.id,
                name.text(), alpha);
    if (const float badge = this->badge_width(); badge > 0.0f) {
      const skia::SkRect pill = skia::SkRect::MakeXYWH(box.fRight - 12.0f - badge, box.fTop + 33.0f, badge, 21.0f);
      p.fillRounded(pill, 10.5f, chosen ? text_colour : muted ? skia::colorSetARGB(255, 90, 98, 106) : accent_colour,
                    alpha);
      const std::string count = std::to_string(unread);
      p.textIn(pill, count, 12.0f, chosen ? selected_colour : background, alpha, true,
               (badge - p.measure(count, 12.0f, true)) * 0.5f);
    }
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
    out.fLabel = name.text();
    out.fSelected = chosen;
    out.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    return out;
  }
};

// One message, as Telegram Desktop shows it: a rounded bubble, on the right
// and blue for what was sent from here, on the left otherwise; in a group,
// the sender's name in their colour over the first of a run and their
// avatar beside its last; the time in the bubble's corner.
struct message_bubble : scene::Node {
  bool outgoing = false;
  bool with_avatar = false;
  bool avatar_space = false;
  std::string sender;
  std::string time;
  std::optional<nodes::Text> name;
  nodes::Text text;
  std::optional<nodes::Text> reactions;
  // The bubble itself, as the last layout placed it.
  skia::SkRect bubble = skia::SkRect::MakeEmpty();

  static constexpr float kPadX = 12.0f;
  static constexpr float kPadY = 7.0f;
  static constexpr float kAvatar = 34.0f;
  static constexpr float kMaxWidth = 480.0f;

  message_bubble(const conversation& in, const message& said, bool first_of_run, bool last_of_run)
      : outgoing(said.outgoing), sender(said.sender), time(clock_of(said.at)),
        text(said.redacted ? std::string("(removed)") : said.body.plain + (said.edited ? " (edited)" : ""), 14.5f,
             text_colour) {
    fState.apply({.fillX = true});
    const bool group = is_group(in);
    avatar_space = group && !outgoing;
    with_avatar = avatar_space && last_of_run;
    if (group && !outgoing && first_of_run)
      name.emplace(sender_name(in, said.sender), 13.0f, avatar_colour(said.sender), true);
    text.setWrapped(true);
    time += std::visit(overloaded{[](const delivery::sending&) { return " · sending"; },
                                  [](const delivery::failed&) { return " · not sent"; },
                                  [](const auto&) { return ""; }},
                       said.delivery);
    if (!said.reactions.empty()) {
      std::string line;
      for (const auto& [key, who] : said.reactions)
        line += std::format("{} {}  ", key, who.size());
      reactions.emplace(std::move(line), 13.0f, dim_colour);
    }
  }

  void forEachChild(auto&& f) {
    f(name);
    f(text);
    f(reactions);
  }

  [[nodiscard]] float inner_width(float row) const {
    const float left = avatar_space ? kAvatar + 8.0f : 0.0f;
    return std::max(40.0f, std::min(kMaxWidth, (row - left) * 0.8f) - 2.0f * kPadX);
  }
  [[nodiscard]] float natural_width() const {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return 0.0f;
    const skiff::paint::Painter p(nullptr, *font);
    float widest = 0.0f;
    std::string_view rest = text.text();
    while (true) {
      const auto newline = rest.find('\n');
      widest = std::max(widest, p.measure(std::string(rest.substr(0, newline)), 14.5f));
      if (newline == std::string_view::npos)
        break;
      rest.remove_prefix(newline + 1);
    }
    widest = std::max(widest, p.measure(time, 11.0f) + 8.0f);
    if (name)
      widest = std::max(widest, p.measure(name->text(), 13.0f, true));
    return widest;
  }

  // As tall as the bubble, which is as wide as its text up to most of the
  // row.
  void measure(const skia::SkRect& parent) {
    const float room = this->inner_width(parent.width());
    const float width = std::min(room, this->natural_width() + 1.0f);
    text.setMaxWidth(width);
    text.measure(parent);
    float height = 2.0f * kPadY + text.fState.fHeight + 14.0f;
    if (name)
      height += 18.0f;
    if (reactions)
      height += 18.0f;
    fState.fHeight = height + 2.0f;
    bubble_width = width + 2.0f * kPadX;
  }
  float bubble_width = 0.0f;

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const float left = outgoing ? box.width() - bubble_width : (avatar_space ? kAvatar + 8.0f : 0.0f);
    bubble = skia::SkRect::MakeXYWH(box.fLeft + left, box.fTop, bubble_width, box.height() - 2.0f);
    float y = kPadY;
    if (name) {
      name->setMaxWidth(bubble_width - 2.0f * kPadX);
      name->fState.arrange(left + kPadX, y);
      scene::layout(*name, box);
      y += 18.0f;
    }
    text.fState.arrange(left + kPadX, y);
    scene::layout(text, box);
    y += text.bounds().height();
    if (reactions) {
      reactions->fState.arrange(left + kPadX, y + 2.0f);
      scene::layout(*reactions, box);
    }
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr || bubble.isEmpty())
      return;
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(bubble, 12.0f, outgoing ? selected_colour : bubble_colour, alpha);
    const float width = p.measure(time, 11.0f);
    p.text(time, bubble.fRight - kPadX - width, bubble.fBottom - 6.0f, 11.0f,
           outgoing ? sent_time_colour : dim_colour, alpha);
    if (with_avatar)
      draw_avatar(canvas,
                  skia::SkRect::MakeXYWH(fState.fBounds.fLeft, fState.fBounds.fBottom - kAvatar - 2.0f, kAvatar, kAvatar),
                  sender, name ? name->text() : sender, alpha);
  }
};

// Something not there yet, said in a box over the window.
template <class Actions>
struct notice_box : scene::Node {
  nodes::Text title{"Not implemented yet", 17.0f, text_colour, true};
  nodes::Text note;
  widgets::Button<ask<Actions, &Actions::close_notice>> ok;

  notice_box(Actions* a, std::string what)
      : note(std::format("{} isn't implemented yet.", what), 14.0f, dim_colour), ok("OK", {a}) {
    fState.apply({.fill = true});
    note.setWrapped(true);
    ok.setPrimary(true);
    ok.apply({.width = 90.0f, .height = 34.0f});
  }
  void forEachChild(auto&& f) {
    f(title);
    f(note);
    f(ok);
  }
  void layoutChildren() {
    const skia::SkRect box = scene::inset(fState.contentBox(), 22.0f, 20.0f);
    title.fState.arrange(0.0f, 0.0f);
    scene::layout(title, box);
    note.setMaxWidth(box.width());
    note.fState.arrange(0.0f, title.bounds().height() + 10.0f);
    scene::layout(note, box);
    ok.fState.arrange(0.0f, 0.0f, scene::anchor::kBottomRight, scene::anchor::kBottomRight);
    scene::layout(ok, box);
  }
};


// What a chat says of itself, over its messages: its avatar, name and who
// is in it or how they are, a line under it, and the button that opens its
// info beside it.
template <class Actions>
struct chat_header : scene::Node {
  std::string key;
  nodes::Text title{"", 15.0f, text_colour, true};
  nodes::Text status{"", 13.0f, dim_colour};
  icon_button<ask<Actions, &Actions::toggle_info>> info;
  nodes::Box<> divider{band_colour};

  static constexpr float kHeight = 56.0f;

  explicit chat_header(Actions* a) : info(icon::info{}, {a}) {
    fState.apply({.fillX = true, .height = kHeight});
    divider.apply({.fillX = true, .height = 1.0f});
    title.setElided(true);
    status.setElided(true);
  }

  void show(const conversation* one, const model& now) {
    info.setVisible(one != nullptr);
    if (one == nullptr) {
      key.clear();
      title.setText("Choose a chat");
      status.setText("");
      return;
    }
    key = one->id.id;
    title.setText(display_name(*one));
    std::string about = is_group(*one) ? std::format("{} member{}", one->members.size(),
                                                     one->members.size() == 1 ? "" : "s")
                                       : presence_of(now, one->id.account, one->id.id);
    if (!one->typing.empty())
      about = one->typing.size() == 1 ? sender_name(*one, one->typing.front()) + " is typing…"
                                      : std::format("{} are typing…", one->typing.size());
    status.setText(std::move(about));
  }

  void forEachChild(auto&& f) {
    f(title);
    f(status);
    f(info);
    f(divider);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const float left = key.empty() ? 16.0f : 64.0f;
    info.fState.arrange(-10.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(info, box);
    title.setMaxWidth(std::max(0.0f, box.width() - left - 60.0f));
    title.fState.arrange(left, key.empty() ? 17.0f : 9.0f);
    scene::layout(title, box);
    status.setMaxWidth(std::max(0.0f, box.width() - left - 60.0f));
    status.fState.arrange(left, 31.0f);
    scene::layout(status, box);
    divider.fState.arrange(0.0f, 0.0f, scene::anchor::kBottomLeft, scene::anchor::kBottomLeft);
    scene::layout(divider, box);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    p.fillRounded(box, 0.0f, sidebar_colour, alpha);
    if (!key.empty())
      draw_avatar(canvas, skia::SkRect::MakeXYWH(box.fLeft + 14.0f, box.centerY() - 19.0f, 38.0f, 38.0f), key,
                  title.text(), alpha);
  }
};

// One of the square buttons of a chat's info: its icon over its name.
template <class Act>
struct action_tile : scene::Node {
  Act act;
  icon_t icon;
  nodes::Text label;

  action_tile(std::string text, icon_t mark, Act what = {})
      : act(std::move(what)), icon(mark), label(std::move(text), 12.0f, text_colour) {
    fState.apply({.height = 58.0f});
  }

  void forEachChild(auto&& f) { f(label); }
  void layoutChildren() {
    label.fState.arrange(0.0f, -8.0f, scene::anchor::kBottomCentre, scene::anchor::kBottomCentre);
    scene::layout(label, fState.contentBox());
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    p.fillRounded(box, 8.0f, fState.fHovered || this->showsFocus() ? chosen_colour : tile_colour, alpha);
    draw_icon(canvas, icon, skia::SkRect::MakeXYWH(box.fLeft, box.fTop + 6.0f, box.width(), 24.0f), text_colour, alpha);
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
// role in a pill.
struct member_row : scene::Node {
  std::string id;
  std::optional<std::string> role;
  nodes::Text name;
  nodes::Text state;

  member_row(const member& one, std::string how)
      : id(one.id), role(one.role), name(one.name.empty() ? one.id : one.name, 14.0f, text_colour, true),
        state(std::move(how), 12.0f, dim_colour) {
    fState.apply({.fillX = true, .height = 54.0f});
    name.setElided(true);
    state.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(name);
    f(state);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const float room = std::max(0.0f, box.width() - 68.0f - (role ? 80.0f : 16.0f));
    name.setMaxWidth(room);
    name.fState.arrange(68.0f, 9.0f);
    scene::layout(name, box);
    state.setMaxWidth(room);
    state.fState.arrange(68.0f, 30.0f);
    scene::layout(state, box);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (fState.fHovered)
      p.fillRounded(box, 0.0f, chosen_colour, alpha);
    draw_avatar(canvas, skia::SkRect::MakeXYWH(box.fLeft + 16.0f, box.centerY() - 20.0f, 40.0f, 40.0f), id,
                name.text(), alpha);
    if (role) {
      const float width = p.measure(*role, 12.0f) + 16.0f;
      const skia::SkRect pill = skia::SkRect::MakeXYWH(box.fRight - 16.0f - width, box.fTop + 10.0f, width, 20.0f);
      p.fillRounded(pill, 10.0f, skia::colorSetARGB(255, 62, 52, 96), alpha);
      p.textIn(pill, *role, 12.0f, skia::colorSetARGB(255, 190, 170, 250), alpha, false, 8.0f);
    }
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
};

// A chat's info, beside it, as Telegram Desktop shows it: a big avatar, the
// name and who is in it, three square buttons, its ID, and its members.
template <class Actions>
struct info_panel : scene::Node {
  std::string key;
  bool group = false;
  icon_button<ask<Actions, &Actions::toggle_info>> close;
  nodes::Text name{"", 17.0f, text_colour, true};
  nodes::Text status{"", 13.0f, dim_colour};
  action_tile<ask<Actions, &Actions::toggle_mute>> mute;
  action_tile<not_yet<Actions>> manage;
  action_tile<ask<Actions, &Actions::leave_chat>> leave;
  nodes::Text id_text{"", 14.0f, accent_colour};
  nodes::Text id_label{"ID", 12.0f, dim_colour};
  nodes::Text members_title{"", 13.0f, dim_colour, true};
  icon_button<not_yet<Actions>> add_member;
  std::vector<member_row> members;
  // Where the bands between the sections go, as the last layout put them.
  std::array<float, 2> bands{};

  static constexpr float kWidth = 340.0f;

  explicit info_panel(Actions* a)
      : close(icon::close{}, {a}),
        mute("Mute", icon::bell{}, {a}),
        manage("Manage", icon::sliders{}, {a, "Managing a chat"}),
        leave("Leave", icon::leave{}, {a}),
        add_member(icon::add_person{}, {a, "Adding members"}) {
    fState.apply({.masking = true});
    name.setElided(true);
    status.setElided(true);
    id_text.setElided(true);
  }

  void show(const conversation& one, const model& now, bool muted) {
    mute.label.setText(muted ? "Unmute" : "Mute");
    key = one.id.id;
    group = is_group(one);
    name.setText(display_name(one));
    status.setText(group ? std::format("{} member{}", one.members.size(), one.members.size() == 1 ? "" : "s")
                         : presence_of(now, one.id.account, one.id.id));
    id_text.setText(one.id.id);
    members_title.setText(std::format("{} MEMBER{}", one.members.size(), one.members.size() == 1 ? "" : "S"));
    members.clear();
    for (const member& each : one.members)
      members.emplace_back(each, presence_of(now, one.id.account, each.id));
    members_title.setVisible(group);
    add_member.setVisible(group);
    this->invalidateLayout();
  }

  void forEachChild(auto&& f) {
    f(close);
    f(name);
    f(status);
    f(mute);
    f(manage);
    f(leave);
    f(id_text);
    f(id_label);
    f(members_title);
    f(add_member);
    f(members);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    close.fState.arrange(-8.0f, 8.0f, scene::anchor::kTopRight, scene::anchor::kTopRight);
    scene::layout(close, box);
    float y = 24.0f + 96.0f + 14.0f;
    for (nodes::Text* centred : {&name, &status}) {
      centred->setMaxWidth(box.width() - 40.0f);
      centred->fState.arrange(0.0f, y, scene::anchor::kTopCentre, scene::anchor::kTopCentre);
      scene::layout(*centred, box);
      y += centred->bounds().height() + 2.0f;
    }
    y += 16.0f;
    const float tile = (box.width() - 32.0f - 16.0f) / 3.0f;
    float x = 16.0f;
    const auto place_tile = [&](auto& each) {
      each.apply({.width = tile});
      each.fState.arrange(x, y);
      scene::layout(each, box);
      x += tile + 8.0f;
    };
    place_tile(mute);
    place_tile(manage);
    place_tile(leave);
    y += 58.0f + 16.0f;
    bands[0] = y;
    y += 6.0f + 14.0f;
    id_text.setMaxWidth(box.width() - 40.0f);
    id_text.fState.arrange(20.0f, y);
    scene::layout(id_text, box);
    y += id_text.bounds().height() + 2.0f;
    id_label.fState.arrange(20.0f, y);
    scene::layout(id_label, box);
    y += id_label.bounds().height() + 16.0f;
    bands[1] = y;
    y += 6.0f;
    if (!group)
      return;
    const skia::SkRect head = skia::SkRect::MakeXYWH(box.fLeft, box.fTop + y, box.width(), 48.0f);
    members_title.fState.arrange(56.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(members_title, head);
    add_member.fState.arrange(-10.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(add_member, head);
    y += 48.0f;
    for (member_row& each : members) {
      each.fState.arrange(0.0f, y);
      scene::layout(each, box);
      y += each.bounds().height();
    }
  }

  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    p.fillRounded(box, 0.0f, sidebar_colour, alpha);
    p.fillRounded(skia::SkRect::MakeXYWH(box.fLeft, box.fTop, 1.0f, box.height()), 0.0f, band_colour, alpha);
    draw_avatar(canvas, skia::SkRect::MakeXYWH(box.centerX() - 48.0f, box.fTop + 24.0f, 96.0f, 96.0f), key,
                name.text(), alpha);
    for (const float y : bands)
      p.fillRounded(skia::SkRect::MakeXYWH(box.fLeft + 1.0f, box.fTop + y, box.width() - 1.0f, 6.0f), 0.0f,
                    section_colour, alpha);
    if (group)
      draw_icon(canvas, icon::people{}, skia::SkRect::MakeXYWH(box.fLeft + 16.0f, box.fTop + bands[1] + 3.0f, 28.0f, 48.0f),
                dim_colour, alpha);
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
struct composer_bar : scene::Node {
  nodes::Box<> divider{band_colour};
  icon_button<not_yet<Actions>> attach;
  widgets::TextArea<submit_message<Actions>> field;
  icon_button<ask<Actions, &Actions::send_typed>> send;

  static constexpr float kSide = 50.0f;
  static constexpr float kPadY = 9.0f;

  explicit composer_bar(Actions* a)
      : attach(icon::clip{}, {a, "Sending files"}), field("Write a message…", {a}), send(icon::send{}, {a}) {
    fState.apply({.fillX = true});
    divider.apply({.fillX = true, .height = 1.0f});
    send.colour = accent_colour;
  }

  [[nodiscard]] const std::string& text() const { return field.text(); }
  void clear() { field.setText({}); }

  void forEachChild(auto&& f) {
    f(divider);
    f(attach);
    f(field);
    f(send);
  }

  // As tall as the text in it, and a margin.
  void measure(const skia::SkRect& parent) {
    field.apply({.width = std::max(0.0f, parent.width() - 2.0f * kSide)});
    field.measure(parent);
    fState.fHeight = std::max(field.fState.fHeight + 2.0f * kPadY, 54.0f);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    divider.fState.arrange(0.0f, 0.0f);
    scene::layout(divider, box);
    attach.fState.arrange(8.0f, -9.0f, scene::anchor::kBottomLeft, scene::anchor::kBottomLeft);
    scene::layout(attach, box);
    send.fState.arrange(-8.0f, -9.0f, scene::anchor::kBottomRight, scene::anchor::kBottomRight);
    scene::layout(send, box);
    field.fState.arrange(kSide, kPadY);
    scene::layout(field, box);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    skiff::paint::Painter(canvas, *font).fillRounded(fState.fBounds, 0.0f, sidebar_colour, alpha);
  }
};

template <class Actions>
struct conversations_screen : scene::Node {
  Actions* actions = nullptr;
  std::optional<conversation_id> chosen;
  // The account whose chats are listed.
  std::optional<account_id> current;
  bool info_open = false;
  // How wide the chat list is: its own, whatever the window's size, until
  // its edge is dragged.
  float side_width = 300.0f;

  nodes::Box<> sidebar{sidebar_colour};
  drag_edge<resize_sidebar_to<Actions>> edge;
  drag_edge<resize_info_to<Actions>> info_edge;
  // How wide the chat's info is, until its edge is dragged.
  float info_width = 340.0f;
  menu_button<Actions> menu;
  nodes::Text name{"mux", 17.0f, text_colour, true};
  nodes::Text no_chats{"No chats yet.", 13.0f, dim_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<conversation_row<Actions>>>> list{
      nodes::Flow<std::vector<conversation_row<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
  chat_header<Actions> header;
  nodes::ScrollContainer<nodes::Flow<std::vector<message_bubble>>> timeline{
      nodes::Flow<std::vector<message_bubble>>({.spacingY = 3.0f, .wrap = false}, {})};
  composer_bar<Actions> line;
  info_panel<Actions> info;
  // What the main area says with no account at all.
  nodes::Text empty_title{"No accounts yet", 22.0f, text_colour, true};
  nodes::Text empty_note{"Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, dim_colour};
  widgets::Button<ask<Actions, &Actions::open_new_account>> empty_add;

  static constexpr float kMinSidebar = 240.0f;
  static constexpr float kHeader = 52.0f;
  static constexpr float kPad = 8.0f;

  explicit conversations_screen(Actions* a)
      : actions(a), edge({a}), info_edge({a}, false), menu(a), header(a), line(a), info(a), empty_add("Add account", {a}) {
    fState.apply({.fill = true});
    sidebar.apply({.fill = true});
    empty_add.setPrimary(true);
    empty_add.apply({.width = 140.0f, .height = 36.0f});
    empty_note.setWrapped(true);
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    std::get<0>(timeline.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    info.setVisible(false);
  }

  void forEachChild(auto&& f) {
    f(sidebar);
    f(menu);
    f(name);
    f(no_chats);
    f(list);
    f(header);
    f(timeline);
    f(line);
    f(info);
    f(empty_title);
    f(empty_note);
    f(empty_add);
    f(edge);  // last: over the list and the chat, where they meet
    f(info_edge);
  }

  // The chat list as wide as `x`, where its edge was dragged to.
  void resize_sidebar(float x) {
    side_width = x - fState.contentBox().fLeft;
    this->invalidateLayout();
  }

  // The chat's info as wide as from `x` to the window's right.
  void resize_info(float x) {
    info_width = fState.contentBox().fRight - x;
    this->invalidateLayout();
  }

  void toggle_info() {
    info_open = !info_open;
    this->invalidateLayout();
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    // Its own width, as far as the window has room for it.
    const float width = std::clamp(side_width, std::min(kMinSidebar, box.width()), std::max(kMinSidebar, box.width() * 0.6f));
    const skia::SkRect side = skia::SkRect::MakeLTRB(box.fLeft, box.fTop, box.fLeft + width, box.fBottom);
    skia::SkRect main = skia::SkRect::MakeLTRB(side.fRight, box.fTop, box.fRight, box.fBottom);
    scene::layout(sidebar, side);
    edge.apply({.width = 7.0f, .height = side.height()});
    edge.fState.arrange(side.fRight - box.fLeft - 3.5f, 0.0f);
    scene::layout(edge, box);

    // The header: the drawer's button, and the name beside it.
    const skia::SkRect head = skia::SkRect::MakeLTRB(side.fLeft + kPad, side.fTop, side.fRight - kPad, side.fTop + kHeader);
    menu.fState.arrange(0.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(menu, head);
    name.fState.arrange(menu.bounds().width() + 10.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(name, head);

    const skia::SkRect below = skia::SkRect::MakeLTRB(side.fLeft, head.fBottom, side.fRight, side.fBottom);
    no_chats.fState.arrange(16.0f, 12.0f);
    scene::layout(no_chats, below);
    list.apply({.width = below.width(), .height = std::max(0.0f, below.height())});
    list.fState.arrange(0.0f, 0.0f);
    scene::layout(list, below);

    // No account at all: what to do about it, in the middle of the rest, a
    // little above its centre.
    if (empty_title.visible()) {
      column_stack stack{form_column(main, 420.0f, std::max(24.0f, main.height() * 0.3f))};
      empty_note.setMaxWidth(stack.column.width());
      stack(empty_title, 10.0f);
      stack(empty_note, 20.0f);
      stack(empty_add, 0.0f);
      return;
    }

    // The chat's info, beside it, where there is room for it.
    const float info_room = std::clamp(info_width, 260.0f, std::max(260.0f, main.width() - 300.0f));
    const bool with_info = info_open && chosen.has_value() && main.width() > 260.0f + 240.0f;
    info.setVisible(with_info);
    info_edge.setVisible(with_info);
    if (with_info) {
      const skia::SkRect right = skia::SkRect::MakeLTRB(main.fRight - info_room, main.fTop, main.fRight, main.fBottom);
      info_edge.apply({.width = 7.0f, .height = right.height()});
      info_edge.fState.arrange(right.fLeft - box.fLeft - 3.5f, 0.0f);
      scene::layout(info_edge, box);
      info.apply({.width = right.width(), .height = right.height()});
      info.fState.arrange(0.0f, 0.0f);
      scene::layout(info, right);
      main.fRight = right.fLeft;
    }

    header.fState.arrange(0.0f, 0.0f);
    scene::layout(header, main);
    line.fState.arrange(0.0f, 0.0f, scene::anchor::kBottomLeft, scene::anchor::kBottomLeft);
    scene::layout(line, main);
    const skia::SkRect inner = skia::SkRect::MakeLTRB(main.fLeft + 12.0f, header.bounds().fBottom + 8.0f,
                                                      main.fRight - 12.0f, line.bounds().fTop - 8.0f);
    timeline.apply({.width = inner.width(), .height = std::max(0.0f, inner.height())});
    timeline.fState.arrange(inner.fLeft - box.fLeft, inner.fTop - box.fTop);
    scene::layout(timeline, box);
  }

  // The model as it is now: the current account's chats, newest first, and
  // the chosen one.
  // The chats muted, as the program keeps them.
  std::set<conversation_id> muted;

  void show(const model& now) {
    if (!current || !now.accounts().contains(*current))
      current = now.accounts().empty() ? std::nullopt : std::optional<account_id>(now.accounts().begin()->first);
    auto& rows = std::get<0>(std::get<0>(list.fChildren).fChildren);
    rows.clear();
    std::vector<const conversation*> chats;
    if (current)
      for (const auto& [key, one] : now.accounts().at(*current).conversations)
        chats.push_back(&one);
    std::ranges::sort(chats, std::ranges::greater{}, [](const conversation* one) {
      return one->timeline.empty() ? std::chrono::sys_time<std::chrono::milliseconds>{} : one->timeline.back().at;
    });
    for (const conversation* one : chats)
      rows.emplace_back(actions, *one, chosen && *chosen == one->id, muted.contains(one->id));
    const bool none = now.accounts().empty();
    for (scene::Node* shown : std::initializer_list<scene::Node*>{&header, &timeline, &line})
      shown->setVisible(!none);
    no_chats.setVisible(!none && chats.empty());
    empty_title.setVisible(none);
    empty_note.setVisible(none);
    empty_add.setVisible(none);
    this->invalidateLayout();
    this->show_conversation(now);
  }

  void show_conversation(const model& now) {
    auto& entries = std::get<0>(std::get<0>(timeline.fChildren).fChildren);
    entries.clear();
    const conversation* one = chosen ? now.find(*chosen) : nullptr;
    header.show(one, now);
    if (!one)
      return;
    info.show(*one, now, muted.contains(one->id));
    const auto& all = one->timeline;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const auto same = [&](std::size_t j) {
        return j < all.size() && all[j].sender == all[i].sender && all[j].outgoing == all[i].outgoing;
      };
      entries.emplace_back(*one, all[i], i == 0 || !same(i - 1), !same(i + 1));
    }
    // The newest is at the bottom, and that is where the reader is.
    timeline.scrollTo(std::numeric_limits<float>::max());
  }
};

// ---- a form row: a caption and a field -----------------------------------

struct field : scene::Node {
  nodes::Text caption;
  widgets::TextBox<> box;

  field(std::string label, std::string placeholder, std::string text = {})
      : caption(std::move(label), 13.0f, dim_colour), box(std::move(placeholder)) {
    fState.apply({.fillX = true, .height = 64.0f});
    box.apply({.fillX = true});
    box.setText(std::move(text));
  }
  void forEachChild(auto&& f) {
    f(caption);
    f(box);
  }
  void layoutChildren() {
    const skia::SkRect area = fState.contentBox();
    caption.fState.arrange(0.0f, 0.0f);
    scene::layout(caption, area);
    box.fState.arrange(0.0f, caption.bounds().height() + 4.0f);
    scene::layout(box, area);
  }
};

// ---- the account forms ---------------------------------------------------------

// What every account form ends with: what went wrong or what is happening,
// and its buttons. Enter in any of the form's fields submits it.
template <class Actions>
struct form_end {
  nodes::Text message{"", 13.0f, error_colour};
  widgets::Button<ask<Actions, &Actions::submit_login>> submit;
  widgets::Button<ask<Actions, &Actions::pop_panel>> close;

  form_end(Actions* a, bool editing) : submit(editing ? "Save" : "Log in", {a}), close("Close", {a}) {
    submit.setPrimary(true);
    submit.apply({.width = 120.0f, .height = 36.0f});
    close.apply({.width = 120.0f, .height = 36.0f});
    close.setVisible(editing);
    message.setWrapped(true);
    message.apply({.fillX = true});
  }

  void each(auto&& f) {
    f(message);
    f(submit);
    f(close);
  }

  void say(std::string text, bool error) {
    message.setText(std::move(text));
    message.setColour(error ? error_colour : dim_colour);
  }

  void place(column_stack& stack) {
    stack(message, 12.0f);
    submit.fState.arrange(0.0f, stack.y);
    scene::layout(submit, stack.column);
    close.fState.arrange(submit.bounds().width() + 12.0f, stack.y);
    scene::layout(close, stack.column);
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
struct xmpp_advanced : scene::Node {
  field resource{"Resource", "mux", "mux"};
  field host{"Host", "from the domain's SRV records"};
  field port{"Port", "5222"};
  widgets::Toggle<ask<Actions, &Actions::toggle_plain>> plain;
  nodes::Text plain_label{"Allow PLAIN without TLS. Only for a test server on this machine: never over a network.",
                          13.0f, error_colour};
  // Its height unfolded, as the last layout found it.
  float full = 0.0f;

  explicit xmpp_advanced(Actions* a) : plain({a}) {
    fState.apply({.fillX = true});
    plain_label.setWrapped(true);
  }

  void measure(const skia::SkRect&) { fState.fHeight = full; }

  void forEachChild(auto&& f) {
    f(resource);
    f(host);
    f(port);
    f(plain);
    f(plain_label);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    column_stack stack{skia::SkRect::MakeXYWH(box.fLeft, box.fTop, box.width(), 10000.0f)};
    stack(resource, 8.0f);
    stack(host, 8.0f);
    stack(port, 8.0f);
    plain.fState.arrange(0.0f, stack.y);
    scene::layout(plain, stack.column);
    const float beside = plain.bounds().width() + 10.0f;
    plain_label.setMaxWidth(std::max(0.0f, stack.column.width() - beside));
    plain_label.fState.arrange(beside, stack.y);
    scene::layout(plain_label, stack.column);
    full = stack.y + std::max(plain.bounds().height(), plain_label.bounds().height()) + 12.0f;
  }
};

// An XMPP account's settings: its JID and password, and "Advanced" folds out
// the rest. It fills the column it is given.
template <class Actions>
struct xmpp_form : scene::Node {
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
    fState.apply({.fill = true});
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
    end.each(f);
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

  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(address, 12.0f);
    stack(password, 12.0f);
    stack(advanced_button, 12.0f);
    stack(more, 0.0f);
    end.place(stack);
  }
};

// A Matrix account's settings: its user ID and password, the homeserver
// (found through the server's .well-known when left empty) and what this
// device is called.
template <class Actions>
struct matrix_form : scene::Node {
  Actions* actions = nullptr;
  std::optional<std::string> editing;

  field user_id{"User ID", "@user:example.org"};
  field password{"Password", "Password"};
  field homeserver{"Homeserver", "found through the server's .well-known"};
  field device_name{"Device name", "mux", "mux"};
  form_end<Actions> end;

  matrix_form(Actions* a, const std::optional<config::matrix_account>& from)
      : actions(a), end(a, from.has_value()) {
    fState.apply({.fill = true});
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
    end.each(f);
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

  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(user_id, 12.0f);
    stack(password, 12.0f);
    stack(homeserver, 12.0f);
    stack(device_name, 12.0f);
    end.place(stack);
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
struct closes_on_escape : scene::Node {
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

// Adding an account, beside the list of them: XMPP or Matrix at the top, and
// that protocol's form under it.
template <class Actions>
struct add_account_pane : scene::Node {
  Actions* actions = nullptr;
  // XMPP | Matrix: two segments in a thin frame.
  nodes::Box<> segments{chosen_colour};
  segment<ask<Actions, &Actions::add_xmpp>> xmpp_tab;
  segment<ask<Actions, &Actions::add_matrix>> matrix_tab;
  nodes::Text note{"", 13.0f, dim_colour};
  account_form<Actions> form;
  // The form coming in when the protocol changes: from the side of the
  // segment chosen, fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  float swap_from = 0.0f;

  explicit add_account_pane(Actions* a)
      : actions(a), xmpp_tab("XMPP", {a}), matrix_tab("Matrix", {a}),
        form(std::in_place_index<0>, a, std::nullopt) {
    this->fState.apply({.fill = true});
    segments.apply({.width = 187.0f, .height = 30.0f});
    note.setWrapped(true);
    this->light();
  }

  void forEachChild(auto&& f) {
    f(segments);
    f(xmpp_tab);
    f(matrix_tab);
    f(note);
    f(form);
  }

  void show_xmpp() {
    form.template emplace<0>(this->actions, std::nullopt);
    this->begin_swap(-1.0f);
    this->light();
  }
  void show_matrix() {
    form.template emplace<1>(this->actions, std::nullopt);
    this->begin_swap(1.0f);
    this->light();
  }
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
  [[nodiscard]] xmpp_form<Actions>* xmpp() { return xmpp_form_in(form); }

  // The tab of the form that is up, lit, and what that protocol is.
  void light() {
    std::visit(overloaded{[this](const xmpp_form<Actions>&) {
                            xmpp_tab.set_active(true);
                            matrix_tab.set_active(false);
                            note.setText("An address like user@example.com, on a server such as Prosody or ejabberd.");
                          },
                          [this](const matrix_form<Actions>&) {
                            xmpp_tab.set_active(false);
                            matrix_tab.set_active(true);
                            note.setText("A user ID like @user:example.org, on a homeserver such as Synapse.");
                          }},
               form);
    this->invalidateLayout();
  }

  void layoutChildren() {
    column_stack stack{this->fState.contentBox()};
    segments.fState.arrange(0.0f, stack.y);
    scene::layout(segments, stack.column);
    xmpp_tab.fState.arrange(1.0f, stack.y + 1.0f);
    scene::layout(xmpp_tab, stack.column);
    matrix_tab.fState.arrange(xmpp_tab.bounds().width() + 2.0f, stack.y + 1.0f);
    scene::layout(matrix_tab, stack.column);
    stack.y += segments.bounds().height() + 12.0f;
    note.setMaxWidth(stack.column.width());
    stack(note, 16.0f);
    const float value = swap.value();
    const float dx = (1.0f - value) * 32.0f * swap_from;
    place_form(form, skia::SkRect::MakeXYWH(stack.column.fLeft + dx, stack.column.fTop, stack.column.width(), stack.column.height()), stack.y);
    std::visit([value](auto& one) { one.fState.setAlpha(value); }, form);
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
struct account_entry : scene::Node {
  Actions* actions = nullptr;
  std::string address;
  bool selected = false;
  nodes::Box<> plate{sidebar_colour};
  nodes::Text name;
  nodes::Text state;

  account_entry(Actions* a, const config::account_t& saved, const model& now, bool is_selected)
      : actions(a), address(config::address_of(saved)), selected(is_selected),
        name(address, 15.0f, text_colour, true), state("", 13.0f, dim_colour) {
    fState.apply({.fillX = true, .height = 52.0f});
    plate.apply({.fill = true});
    plate.setColour(selected ? chosen_colour : sidebar_colour);
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    name.setElided(true);
    state.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(plate);
    f(name);
    f(state);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    scene::layout(plate, box);
    const skia::SkRect inner = scene::inset(box, 10.0f, 7.0f);
    name.setMaxWidth(inner.width());
    name.fState.arrange(0.0f, 0.0f);
    scene::layout(name, inner);
    state.setMaxWidth(inner.width());
    state.fState.arrange(0.0f, name.bounds().height() + 4.0f);
    scene::layout(state, inner);
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
struct account_editor : scene::Node {
  nodes::Text heading;
  nodes::Text state{"", 13.0f, dim_colour};
  widgets::Toggle<flip_account<Actions>> enabled;
  nodes::Text enabled_label{"On", 13.0f, dim_colour};
  widgets::Button<remove_account<Actions>> remove;
  account_form<Actions> form;

  account_editor(Actions* a, const config::account_t& saved)
      : heading(config::address_of(saved), 20.0f, text_colour, true),
        enabled(flip_account<Actions>{a, config::address_of(saved)}),
        remove("Remove", remove_account<Actions>{a, config::address_of(saved)}),
        form(form_of(a, saved)) {
    fState.apply({.fill = true});
    heading.setElided(true);
    state.setElided(true);
    enabled.setOnNow(config::enabled_of(saved));
    remove.apply({.width = 100.0f, .height = 32.0f});
  }

  void forEachChild(auto&& f) {
    f(heading);
    f(state);
    f(enabled);
    f(enabled_label);
    f(remove);
    f(form);
  }

  // What the model says of it now, kept current without touching the form.
  void show(const config::account_t& saved, const model& now) {
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    enabled.setOn(config::enabled_of(saved));
  }

  void say(std::string text, bool error) {
    std::visit([&](auto& one) { one.say(std::move(text), error); }, form);
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    remove.fState.arrange(0.0f, 0.0f, scene::anchor::kTopRight, scene::anchor::kTopRight);
    scene::layout(remove, box);
    enabled.fState.arrange(-(remove.bounds().width() + 16.0f), 5.0f, scene::anchor::kTopRight,
                           scene::anchor::kTopRight);
    scene::layout(enabled, box);
    enabled_label.fState.arrange(enabled.bounds().fLeft - box.fLeft - 30.0f, 7.0f);
    scene::layout(enabled_label, box);
    heading.setMaxWidth(std::max(0.0f, enabled_label.bounds().fLeft - box.fLeft - 12.0f));
    heading.fState.arrange(0.0f, 0.0f);
    scene::layout(heading, box);
    state.setMaxWidth(box.width());
    state.fState.arrange(0.0f, heading.bounds().height() + 6.0f);
    scene::layout(state, box);
    place_form(form, box, state.bounds().fBottom - box.fTop + 20.0f);
  }
};

// A line with a switch on its right: its text, and the switch.
template <class Act>
struct switch_row : scene::Node {
  nodes::Text label;
  widgets::Toggle<Act> toggle;

  switch_row(std::string text, Act what) : label(std::move(text), 15.0f, text_colour), toggle(std::move(what)) {
    fState.apply({.fillX = true, .height = row_item<nothing>::kHeight});
  }
  void forEachChild(auto&& f) {
    f(label);
    f(toggle);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    toggle.fState.arrange(-20.0f, 0.0f, scene::anchor::kCentreRight, scene::anchor::kCentreRight);
    scene::layout(toggle, box);
    label.setMaxWidth(std::max(0.0f, box.width() - 90.0f));
    label.fState.arrange(20.0f, 0.0f, scene::anchor::kCentreLeft, scene::anchor::kCentreLeft);
    scene::layout(label, box);
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
struct account_pages : scene::Node {
  row_item<choose_account_page<Actions>> connection;
  row_item<choose_account_page<Actions>> privacy;
  row_item<choose_account_page<Actions>> proxy;

  explicit account_pages(Actions* a)
      : connection("Connection", {a, 0}, icon::sliders{}),
        privacy("Privacy", {a, 1}, icon::eye{}),
        proxy("Proxy", {a, 2}, icon::gear{}) {
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
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack.y = 6.0f;
    stack(connection, 0.0f);
    stack(privacy, 0.0f);
    stack(proxy, 0.0f);
  }
};

// A section's title on a settings page, as Gajim sets them: small, bold, dim.
inline nodes::Text section_title(std::string text) { return nodes::Text(std::move(text), 13.0f, dim_colour, true); }

// An account's Privacy page: whether it sends read receipts.
template <class Actions>
struct account_privacy : scene::Node {
  nodes::Text title = section_title("PRIVACY");
  switch_row<ask<Actions, &Actions::flip_account_receipts>> receipts;
  nodes::Text note{"Off, the people you talk to through this account are not told when you have read their "
                   "messages -- nor, on most servers, are you told when they have read yours.",
                   13.0f, dim_colour};

  account_privacy(Actions* a, bool on) : receipts("Send read receipts", {a}) {
    fState.apply({.fill = true});
    note.setWrapped(true);
    receipts.toggle.setOnNow(on);
  }
  void show(bool on) { receipts.toggle.setOn(on); }
  void say(std::string, bool) {}
  void forEachChild(auto&& f) {
    f(title);
    f(receipts);
    f(note);
  }
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(title, 4.0f);
    stack(receipts, 8.0f);
    note.setMaxWidth(stack.column.width());
    stack(note, 0.0f);
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
struct account_proxy : scene::Node {
  nodes::Text title = section_title("PROXY");
  std::vector<row_item<choose_account_proxy<Actions>>> choices;
  row_item<ask<Actions, &Actions::manage_proxies>> manage;

  account_proxy(Actions* a, const std::vector<config::proxy_settings>& all, const std::optional<std::string>& current)
      : manage("Manage proxies…", {a}, icon::gear{}) {
    fState.apply({.fill = true});
    choices.emplace_back("No proxy", choose_account_proxy<Actions>{a, -1}, icon::none{}, !current.has_value());
    for (std::size_t i = 0; i < all.size(); ++i)
      choices.emplace_back(std::format("{} ({} {}:{})", all[i].name, all[i].kind == "http" ? "HTTP" : "SOCKS5",
                                       all[i].host, all[i].port),
                           choose_account_proxy<Actions>{a, static_cast<int>(i)}, icon::none{},
                           current && *current == all[i].name);
  }
  void show(bool) {}
  void say(std::string, bool) {}
  void forEachChild(auto&& f) {
    f(title);
    f(choices);
    f(manage);
  }
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(title, 4.0f);
    for (auto& one : choices)
      stack(one, 0.0f);
    stack.y += 8.0f;
    stack(manage, 0.0f);
  }
};

// The saved accounts down the side, and the chosen one's settings beside
// them.
template <class Actions>
struct accounts_panel : closes_on_escape<Actions> {
  static constexpr int kTab = 2;
  static constexpr float kListWidth = 280.0f;
  static constexpr float kPad = 8.0f;

  std::optional<std::string> selected;
  // Its ← goes back from an account's pages to the list, and from the list
  // to the chats.
  page_header<ask<Actions, &Actions::accounts_back>, ask<Actions, &Actions::accounts_back>> header;
  nodes::Box<> side{sidebar_colour};
  nodes::ScrollContainer<nodes::Flow<std::vector<account_entry<Actions>>>> list{
      nodes::Flow<std::vector<account_entry<Actions>>>({.spacingY = 0.0f, .wrap = false}, {})};
  row_item<ask<Actions, &Actions::open_new_account>> add;
  account_pages<Actions> pages;
  nodes::Text message{"", 13.0f, error_colour};
  // No account chosen, or the chosen one.
  std::variant<nodes::Text, account_editor<Actions>, add_account_pane<Actions>, account_privacy<Actions>,
               account_proxy<Actions>>
      detail{
      std::in_place_index<0>, "Choose an account.", 15.0f, dim_colour};
  // What is beside the list coming in when another is chosen: sliding and
  // fading in.
  skiff::paint::Tween swap{1.0f, 200.0f, skiff::paint::movement::subtle{}};

  void begin_swap() {
    swap.jump(0.0f);
    swap.setTarget(1.0f);
  }
  [[nodiscard]] bool settling() const { return swap.moving(); }
  void update(double now_ms) {
    if (swap.step(now_ms))
      this->invalidateLayout();
  }

  explicit accounts_panel(Actions* a)
      : closes_on_escape<Actions>(a), header("Accounts", {a}, {a}, true, false), add("Add account", {a}, icon::plus{}),
        pages(a) {
    pages.setVisible(false);
    this->fState.apply({.fill = true});
    side.apply({.fill = true});
    std::get<0>(list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    message.setWrapped(true);
  }

  void forEachChild(auto&& f) {
    f(side);
    f(header);
    f(list);
    f(add);
    f(pages);
    f(message);
    f(detail);
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
      detail.template emplace<3>(this->actions, config::read_receipts_of(one));
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
    detail.template emplace<2>(this->actions);
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

  void layoutChildren() {
    const skia::SkRect whole = this->fState.contentBox();
    header.fState.arrange(0.0f, 0.0f);
    scene::layout(header, whole);
    const skia::SkRect box = skia::SkRect::MakeLTRB(whole.fLeft, header.bounds().fBottom, whole.fRight, whole.fBottom);
    const float list_width = std::min(kListWidth, box.width() * 0.45f);
    const skia::SkRect left = skia::SkRect::MakeLTRB(box.fLeft, box.fTop, box.fLeft + list_width, box.fBottom);
    scene::layout(side, left);
    pages.apply({.width = left.width(), .height = left.height()});
    pages.fState.arrange(0.0f, 0.0f);
    scene::layout(pages, left);
    add.fState.arrange(0.0f, 0.0f);
    scene::layout(add, left);
    float y = add.bounds().fBottom - box.fTop + 2.0f;
    if (!message.text().empty()) {
      message.setMaxWidth(list_width - 2 * kPad);
      message.fState.arrange(kPad, y);
      scene::layout(message, left);
      y = message.bounds().fBottom - box.fTop + kPad;
    }
    list.apply({.width = list_width, .height = std::max(0.0f, box.height() - y)});
    list.fState.arrange(0.0f, y);
    scene::layout(list, box);

    const skia::SkRect right = skia::SkRect::MakeLTRB(left.fRight, box.fTop, box.fRight, box.fBottom);
    const float value = swap.value();
    const skia::SkRect column = form_column(right, 520.0f, 24.0f);
    const skia::SkRect moved = skia::SkRect::MakeXYWH(column.fLeft + (1.0f - value) * 28.0f, column.fTop, column.width(), column.height());
    std::visit(
        [&](auto& one) {
          one.fState.setAlpha(value);
          one.fState.arrange(0.0f, 0.0f);
          scene::layout(one, moved);
        },
        detail);
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
struct drawer_account : scene::Node {
  Actions* actions = nullptr;
  std::string address;
  bool current = false;
  nodes::Text name;
  nodes::Text state;

  drawer_account(Actions* a, const config::account_t& saved, const model& now, bool is_current)
      : actions(a), address(config::address_of(saved)), current(is_current), name(address, 14.0f, text_colour, true),
        state("", 12.0f, dim_colour) {
    fState.apply({.fillX = true, .height = 56.0f});
    const auto [how, failed] = state_of(saved, now);
    state.setText(std::format("{} · {}", config::protocol_name(saved), how));
    state.setColour(failed ? error_colour : dim_colour);
    name.setElided(true);
    state.setElided(true);
  }

  void forEachChild(auto&& f) {
    f(name);
    f(state);
  }
  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    const skia::SkRect inner = skia::SkRect::MakeLTRB(box.fLeft + 68.0f, box.fTop + 9.0f, box.fRight - 48.0f, box.fBottom);
    name.setMaxWidth(inner.width());
    name.fState.arrange(0.0f, 0.0f);
    scene::layout(name, inner);
    state.setMaxWidth(inner.width());
    state.fState.arrange(0.0f, name.bounds().height() + 3.0f);
    scene::layout(state, inner);
  }
  void drawSelf(skia::SkCanvas* canvas, float alpha) {
    skia::SkFont* font = skiff::paint::defaultFont();
    if (font == nullptr)
      return;
    const skiff::paint::Painter p(canvas, *font);
    const skia::SkRect& box = fState.fBounds;
    if (current || fState.fHovered || this->showsFocus())
      p.fillRounded(box, 0.0f, chosen_colour, alpha);
    draw_avatar(canvas, skia::SkRect::MakeXYWH(box.fLeft + 16.0f, box.centerY() - 19.0f, 38.0f, 38.0f), address,
                address, alpha);
    // The account whose chats are shown: a tick on the right.
    if (current)
      draw_icon(canvas, icon::check{}, skia::SkRect::MakeXYWH(box.fRight - 40.0f, box.fTop, 24.0f, box.height()),
                accent_colour, alpha);
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
struct drawer_panel : scene::Node {
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

  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack.y = 18.0f;
    title.fState.arrange(20.0f, stack.y);
    scene::layout(title, stack.column);
    stack.y += title.bounds().height() + 14.0f;
    // The accounts, and Manage accounts under them, in one section.
    for (auto& one : accounts)
      stack(one, 0.0f);
    stack(manage, 6.0f);
    stack(rule_1, 6.0f);
    stack(settings, 0.0f);
    stack(quit, 0.0f);
  }
};

// ---- the settings -------------------------------------------------------------------

// Settings, as Telegram Desktop shows them: a box over the window, a list of
// sections, and each section a page of the same box.
template <class Actions>
struct settings_home : scene::Node {
  page_header<ask<Actions, &Actions::close_settings>, ask<Actions, &Actions::close_settings>> header;
  row_item<ask<Actions, &Actions::open_accounts>> accounts;
  row_item<ask<Actions, &Actions::settings_animations>> animations;
  row_item<ask<Actions, &Actions::settings_proxies>> proxies;
  row_item<ask<Actions, &Actions::settings_appearance>> appearance;

  explicit settings_home(Actions* a)
      : header("Settings", {a}, {a}, false, true),
        accounts("Accounts", {a}, icon::person{}),
        animations("Animations", {a}, icon::motion{}),
        proxies("Proxies", {a}, icon::gear{}),
        appearance("Appearance", {a}, icon::eye{}) {
    fState.apply({.fill = true});
  }

  void forEachChild(auto&& f) {
    f(header);
    f(accounts);
    f(animations);
    f(appearance);
    f(proxies);
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(header, 6.0f);
    stack(accounts, 0.0f);
    stack(animations, 0.0f);
    stack(appearance, 0.0f);
    stack(proxies, 0.0f);
  }
};



template <class Actions>
struct animations_page : scene::Node {
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
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(header, 4.0f);
    note.setMaxWidth(std::max(0.0f, stack.column.width() - 40.0f));
    note.fState.arrange(20.0f, stack.y);
    scene::layout(note, stack.column);
    stack.y += note.bounds().height() + 12.0f;
    stack(full, 0.0f);
    stack(reduced, 0.0f);
    stack(none, 0.0f);
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
  int kind = 1;
  void operator()() const { actions->proxy_kind(kind); }
};

// Settings' Proxies page, as Gajim's Manage Proxies: the profiles, and a way
// to add one.
template <class Actions>
struct proxies_page : scene::Node {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  std::vector<row_item<edit_proxy<Actions>>> profiles;
  row_item<ask<Actions, &Actions::add_proxy>> add;
  nodes::Text empty{"No proxies yet. Accounts connect directly.", 13.0f, dim_colour};

  proxies_page(Actions* a, const std::vector<config::proxy_settings>& all)
      : header("Proxies", {a}, {a}, true, true), add("Add proxy", {a}, icon::plus{}) {
    fState.apply({.fill = true});
    for (std::size_t i = 0; i < all.size(); ++i)
      profiles.emplace_back(std::format("{} ({} {}:{})", all[i].name, all[i].kind == "http" ? "HTTP" : "SOCKS5",
                                        all[i].host, all[i].port),
                            edit_proxy<Actions>{a, static_cast<int>(i)}, icon::gear{});
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
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(header, 4.0f);
    for (auto& one : profiles)
      stack(one, 0.0f);
    stack(add, 8.0f);
    empty.fState.arrange(20.0f, stack.y);
    scene::layout(empty, stack.column);
  }
};

// One proxy profile's page: its name, SOCKS5 or HTTP, where, and who to be
// there; saved or deleted with its buttons.
template <class Actions>
struct proxy_editor : scene::Node {
  int index = -1;  // in the list; -1 for a new one
  int kind = 1;    // 1 SOCKS5, 2 HTTP
  page_header<ask<Actions, &Actions::settings_proxies>, ask<Actions, &Actions::close_settings>> header;
  field name{"Name", "Home, Tor, Work…"};
  nodes::Box<> frame{chosen_colour};
  segment<choose_proxy_kind<Actions>> socks;
  segment<choose_proxy_kind<Actions>> http;
  field host{"Host", "proxy.example.com"};
  field port{"Port", "1080"};
  field username{"User name", "none"};
  field password{"Password", "none"};
  nodes::Text message{"", 13.0f, dim_colour};
  widgets::Button<ask<Actions, &Actions::save_proxy_profile>> save;
  widgets::Button<ask<Actions, &Actions::delete_proxy_profile>> remove;

  proxy_editor(Actions* a, const std::optional<config::proxy_settings>& from, int at)
      : index(at), header(from ? from->name : std::string("New proxy"), {a}, {a}, true, true),
        socks("SOCKS5", {a, 1}), http("HTTP", {a, 2}), save("Save", {a}), remove("Delete", {a}) {
    fState.apply({.fill = true});
    frame.apply({.width = 2.0f * 92.0f + 3.0f, .height = 30.0f});
    password.box.setMasked(true);
    save.setPrimary(true);
    save.apply({.width = 110.0f, .height = 34.0f});
    remove.apply({.width = 110.0f, .height = 34.0f});
    remove.setVisible(from.has_value());
    message.setWrapped(true);
    if (from) {
      name.box.setText(from->name);
      host.box.setText(from->host);
      port.box.setText(std::to_string(from->port));
      username.box.setText(from->username.value_or(""));
      password.box.setText(from->password.value_or(""));
    }
    this->set_kind(from && from->kind == "http" ? 2 : 1);
  }

  void set_kind(int to) {
    kind = to;
    socks.set_active(kind == 1);
    http.set_active(kind == 2);
  }

  // The profile as typed, or what is wrong with it.
  [[nodiscard]] std::expected<config::proxy_settings, std::string> proxy() const {
    config::proxy_settings out{.name = name.box.text(), .kind = kind == 2 ? "http" : "socks5", .host = host.box.text()};
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
    f(frame);
    f(socks);
    f(http);
    f(host);
    f(port);
    f(username);
    f(password);
    f(message);
    f(save);
    f(remove);
  }
  void layoutChildren() {
    column_stack stack{scene::inset(fState.contentBox(), 16.0f, 0.0f)};
    header.fState.arrange(0.0f, 0.0f);
    scene::layout(header, fState.contentBox());
    stack.y = header.kHeight + 4.0f;
    stack(name, 10.0f);
    frame.fState.arrange(0.0f, stack.y);
    scene::layout(frame, stack.column);
    socks.fState.arrange(1.0f, stack.y + 1.0f);
    scene::layout(socks, stack.column);
    http.fState.arrange(94.0f, stack.y + 1.0f);
    scene::layout(http, stack.column);
    stack.y += 30.0f + 12.0f;
    stack(host, 6.0f);
    stack(port, 6.0f);
    stack(username, 6.0f);
    stack(password, 10.0f);
    message.setMaxWidth(stack.column.width());
    stack(message, 6.0f);
    save.fState.arrange(0.0f, stack.y);
    scene::layout(save, stack.column);
    remove.fState.arrange(save.bounds().width() + 10.0f, stack.y);
    scene::layout(remove, stack.column);
  }
};

// A theme or a renderer chosen on the Appearance page.
template <class Actions>
struct choose_theme {
  Actions* actions = nullptr;
  std::string_view name;
  void operator()() const { actions->set_theme(std::string(name)); }
};
template <class Actions>
struct choose_renderer {
  Actions* actions = nullptr;
  std::string_view name;
  void operator()() const { actions->set_renderer(std::string(name)); }
};

// Settings' Appearance page: the theme, and what draws the window.
template <class Actions>
struct appearance_page : scene::Node {
  page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>> header;
  nodes::Text theme_title = section_title("THEME");
  row_item<choose_theme<Actions>> dark;
  row_item<choose_theme<Actions>> light;
  nodes::Text renderer_title = section_title("RENDERING");
  row_item<choose_renderer<Actions>> gpu;
  row_item<choose_renderer<Actions>> cpu;
  nodes::Text note{"The theme changes at once; what draws the window, when mux starts again.", 13.0f,
                   dim_colour};

  appearance_page(Actions* a, std::string_view theme, std::string_view renderer)
      : header("Appearance", {a}, {a}, true, true),
        dark("Dark", {a, "dark"}, icon::none{}, false),
        light("Light", {a, "light"}, icon::none{}, false),
        gpu("OpenGL (the graphics card)", {a, "opengl"}, icon::none{}, false),
        cpu("Software (the processor)", {a, "software"}, icon::none{}, false) {
    fState.apply({.fill = true});
    this->show(theme, renderer);
  }
  void show(std::string_view theme, std::string_view renderer) {
    dark.set_chosen(theme != "light");
    light.set_chosen(theme == "light");
    gpu.set_chosen(renderer != "software");
    cpu.set_chosen(renderer == "software");
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
  void forEachChild(auto&& f) {
    f(header);
    f(theme_title);
    f(dark);
    f(light);
    f(renderer_title);
    f(gpu);
    f(cpu);
    f(note);
  }
  void layoutChildren() {
    column_stack stack{fState.contentBox()};
    stack(header, 6.0f);
    theme_title.fState.arrange(20.0f, stack.y);
    scene::layout(theme_title, stack.column);
    stack.y += theme_title.bounds().height() + 4.0f;
    stack(dark, 0.0f);
    stack(light, 12.0f);
    renderer_title.fState.arrange(20.0f, stack.y);
    scene::layout(renderer_title, stack.column);
    stack.y += renderer_title.bounds().height() + 4.0f;
    stack(gpu, 0.0f);
    stack(cpu, 10.0f);
    note.fState.arrange(20.0f, stack.y);
    scene::layout(note, stack.column);
  }
};

template <class Actions>
struct settings_dialog : scene::Node {
  Actions* actions = nullptr;
  std::string motion;
  std::variant<settings_home<Actions>, animations_page<Actions>, proxies_page<Actions>, proxy_editor<Actions>,
               appearance_page<Actions>>
      page;

  settings_dialog(Actions* a, std::string level) : actions(a), motion(std::move(level)), page(std::in_place_index<0>, a) {
    fState.apply({.fill = true});
  }

  void forEachChild(auto&& f) { f(page); }

  void show_home() { page.template emplace<0>(actions); }
  void show_animations() {
    page.template emplace<1>(actions);
    this->show_motion(motion);
  }
  void show_appearance(std::string_view theme, std::string_view renderer) {
    page.template emplace<4>(actions, theme, renderer);
  }
  [[nodiscard]] appearance_page<Actions>* appearance() {
    return std::visit(overloaded{[](appearance_page<Actions>& one) { return &one; },
                                 [](auto&) -> appearance_page<Actions>* { return nullptr; }},
                      page);
  }
  void show_proxies(const std::vector<config::proxy_settings>& all) { page.template emplace<2>(actions, all); }
  void show_proxy(const std::optional<config::proxy_settings>& from, int index) {
    page.template emplace<3>(actions, from, index);
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
          scene::layout(one, fState.contentBox());
        },
        page);
  }
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

    explicit parts(Actions* a) : frame(std::piecewise_construct, std::forward_as_tuple(a), std::forward_as_tuple(a)) {
      backdrop.apply({.fill = true});
      frame.setSheetColour(background);
      frame.base().setSheetColour(sidebar_colour);
      settings.setSheetColour(sidebar_colour);
      settings.setSize(440.0f, 520.0f);
      notice.setSheetColour(sidebar_colour);
      notice.setSize(360.0f, 160.0f);
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
  }

  void open_settings(std::string motion) { p->settings.open(actions, std::move(motion)); }
  void close_settings() { p->settings.close(); }
  [[nodiscard]] settings_dialog<Actions>* settings_up() { return p->settings.shown(); }

  void open_drawer() { p->frame.base().open(); }
  void close_drawer() { p->frame.base().close(); }
  void close_drawer_now() { p->frame.base().closeNow(); }
  [[nodiscard]] bool drawer_open() { return p->frame.base().isOpen(); }
  // Whether the pages are still moving.
  [[nodiscard]] bool pages_moving() { return p->frame.settling(); }

  void show_notice(std::string what) { p->notice.open(actions, std::move(what)); }
  void close_notice() { p->notice.close(); }

  void show(const std::vector<config::account_t>& saved, const model& now) {
    const auto& current = p->frame.base().base().current;
    p->frame.base().content().show(actions, saved, now, current ? std::string_view(current->address) : std::string_view());
  }
  void show_motion(std::string_view level) {
    if (auto* up = p->settings.shown())
      up->show_motion(std::string(level));
  }

  void layoutChildren() {
    const skia::SkRect box = fState.contentBox();
    scene::layout(p->backdrop, box);
    p->frame.fState.arrange(0.0f, 0.0f);
    scene::layout(p->frame, box);
    p->settings.fState.arrange(0.0f, 0.0f);
    scene::layout(p->settings, box);
    p->notice.fState.arrange(0.0f, 0.0f);
    scene::layout(p->notice, box);
  }
};

}  // namespace mux::ui
