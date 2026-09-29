// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:appearance -- Settings: appearance and rendering.
export module mux.ui:appearance;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :proxies;

export namespace mux::ui {

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

}  // namespace mux::ui
