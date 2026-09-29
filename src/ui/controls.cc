// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:controls -- The small controls: marks, rows, icon buttons, headers, segments, the drawer button.
export module mux.ui:controls;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :avatars;

export namespace mux::ui {

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
    fState.apply({.fillX = true, .height = kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}, .hoverBackground = chosen_colour, .selectedBackground = chosen_colour, .focusBackground = chosen_colour});
    mark.setVisible(std::visit([](auto one) { return drawn(one); }, icon));
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
    fState.apply({.selected = on});
    this->markDamaged();
  }
  bool lit = false;

  void forEachChild(auto&& f) {
    f(mark);
    f(label);
    f(dot);
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

}  // namespace mux::ui
