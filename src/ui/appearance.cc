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
  // A small picture of it: its background, a bubble of each side, and a
  // ring round it while it is the one in use -- plates placed on the card.
  struct picture_t : scene::Node {
    struct parts_t {
      nodes::Box<> incoming;
      nodes::Box<> outgoing;
    } parts;
    picture_t(skia::SkColor back, skia::SkColor in, skia::SkColor out)
        : parts{.incoming = nodes::Box<>(in), .outgoing = nodes::Box<>(out)} {
      fState.apply({.place = scene::anchor::kTopLeft, .y = -62.0f, .fillX = true, .height = 56.0f, .cornerRadius = 8.0f,
                    .background = back});
      parts.incoming.apply({.place = scene::anchor::kTopLeft, .x = 6.0f, .y = 8.0f, .width = 44.0f, .height = 14.0f,
                            .cornerRadius = 7.0f});
      parts.outgoing.apply({.place = scene::anchor::kTopRight, .x = -6.0f, .y = 30.0f, .width = 44.0f, .height = 14.0f,
                            .cornerRadius = 7.0f});
    }
    void set_ring(bool on) { fState.apply({.border = scene::Border{on ? accent_colour : 0u, on ? 2.0f : 0.0f}}); }
  } picture;
  theme_card(Actions* a, config::theme_t which, std::string label, skia::SkColor b, skia::SkColor in, skia::SkColor out)
      : actions(a), theme(which), back(b), bubble(in), mine(out), name(std::move(label), 12.0f, dim_colour),
        picture(b, in, out) {
    fState.apply({.width = 92.0f, .height = 92.0f, .padding = {66.0f, 6.0f, 0.0f, 6.0f}});
    name.apply({.alignSelf = scene::align::kMiddle});
  }
  void set_chosen(bool on) {
    chosen = on;
    picture.set_ring(on);
  }
  void forEachChild(auto&& f) {
    f(name);
    f(picture);
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
  struct parts_t {
    nodes::Box<> dot;
  } parts;
  accent_circle(Actions* a, config::accent_t which, const config::theme_t& in)
      : actions(a), accent(which), shade(colour_of(which, in)), parts{.dot = nodes::Box<>(shade)} {
    fState.apply({.width = 34.0f, .height = 34.0f, .cornerRadius = 17.0f});
    parts.dot.apply({.place = scene::anchor::kCentre, .width = 24.0f, .height = 24.0f, .cornerRadius = 12.0f});
  }
  // A ring in its shade while it is the one in use.
  void set_chosen(bool on) {
    chosen = on;
    fState.apply({.border = scene::Border{on ? shade : 0u, on ? 2.0f : 0.0f}});
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
      card->set_chosen(card->theme == theme);
    for (auto& circle : circles.circles)
      circle.set_chosen(circle.accent == accent);
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
