// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:appearance -- Settings: appearance and rendering.
export module mux.ui:appearance;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :themes;
import :accounts;
import :proxies;

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
  };
  struct parts_t {
    nodes::Text name;
    picture_t picture;
  } parts;
  theme_card(Actions* a, config::theme_t which, std::string label, skia::SkColor b, skia::SkColor in, skia::SkColor out)
      : actions(a), theme(which), back(b), bubble(in), mine(out),
        parts{.name = nodes::Text(std::move(label), 12.0f, dim_colour), .picture = picture_t(b, in, out)} {
    fState.apply({.width = 92.0f, .height = 92.0f, .padding = {66.0f, 6.0f, 0.0f, 6.0f}});
    parts.name.apply({.alignSelf = scene::align::kMiddle});
  }
  void set_chosen(bool on) {
    chosen = on;
    parts.picture.set_ring(on);
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
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  struct cards_row : nodes::Stack {
    using card = theme_card<Actions>;
    // Telegram's cards, in its order, with its own pictures' colours:
    // the wallpaper, a bubble received, one sent.
    struct parts_t {
      card classic, day, tinted, night;
    } parts;
    explicit cards_row(Actions* a)
        : parts{.classic = card(a, config::theme::classic{}, "Classic", skia::colorSetARGB(255, 155, 212, 148),
                                skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 234, 255, 220)),
                .day = card(a, config::theme::day{}, "Day", skia::colorSetARGB(255, 126, 196, 234),
                            skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 215, 240, 255)),
                .tinted = card(a, config::theme::tinted{}, "Tinted", skia::colorSetARGB(255, 72, 87, 97),
                               skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 92, 167, 212)),
                .night = card(a, config::theme::night{}, "Night", skia::colorSetARGB(255, 72, 87, 97),
                              skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 117, 191, 181))} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}});
    }
  };
  struct circles_row : nodes::Stack {
    struct parts_t {
      std::vector<accent_circle<Actions>> circles;
    } parts;
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
        parts.circles.emplace_back(a, one, in);
    }
  };
  struct parts_t {
    header_t header;
    nodes::Text theme_title = section_title("THEME");
    cards_row cards;
    nodes::Text accent_title = section_title("ACCENT");
    circles_row circles;
    nodes::Text wallpaper_title = section_title("CHAT BACKGROUND");
    widgets::Button<open_wallpaper_at<Actions>> wallpaper;
  } parts;

  appearance_page(Actions* a, const config::theme_t& theme, const config::accent_t& accent)
      : parts{.header = header_t("Appearance", {a}, {a}, true, true),
              .cards = cards_row(a),
              .circles = circles_row(a, theme),
              .wallpaper = widgets::Button<open_wallpaper_at<Actions>>("Chat background\u2026",
                                                                      {a, choice_level::everywhere{}})} {
    fState.apply({.fill = true});
    parts.theme_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    parts.accent_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    parts.wallpaper_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    parts.wallpaper.apply({.width = 200.0f, .height = 34.0f, .margin = {0.0f, 0.0f, 0.0f, 20.0f}});
    this->show(theme, accent);
  }
  void show(const config::theme_t& theme, const config::accent_t& accent) {
    auto& [classic, day, tinted, night] = parts.cards.parts;
    for (auto* card : {&classic, &day, &tinted, &night})
      card->set_chosen(card->theme == theme);
    for (auto& circle : parts.circles.parts.circles)
      circle.set_chosen(circle.accent == accent);
    this->markDamaged();
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};

// Settings' Rendering page: what draws the window, from the next start.
template <class Actions>
struct rendering_page : nodes::Stack {
  using header_t = page_header<ask<Actions, &Actions::settings_home>, ask<Actions, &Actions::close_settings>>;
  using choice = row_item<choose_renderer<Actions>>;
  using partial_row = switch_row<ask<Actions, &Actions::flip_partial_redraw>>;
  using flash_row = switch_row<ask<Actions, &Actions::flip_flash_redraws>>;
  using vsync_row = switch_row<ask<Actions, &Actions::flip_vsync>>;
  using fps_row = switch_row<ask<Actions, &Actions::flip_show_fps>>;
  // What is under the header: it scrolls where the dialog is too low for it.
  struct body : nodes::Stack {
    struct parts_t {
      choice gpu;
      choice cpu;
      nodes::Text note{"Takes effect when mux starts again.", 13.0f, dim_colour};
      nodes::Text frames_title = section_title("FRAMES");
      partial_row partial;
      flash_row flash;
      vsync_row vsync;
      fps_row fps;
      nodes::Text frames_note{"Partial redraw repaints only what changed, into a frame kept between them; a part "
                              "that forgets to say it changed then stays as it was. Flashing outlines what each "
                              "frame repainted. Vsync shows frames in step with the screen; off, they are shown as "
                              "soon as drawn. The counter shows frames a second and the last frame's time. All take "
                              "effect at once.",
                              13.0f, dim_colour};
    } parts;
    body(Actions* a, bool partial, bool flash, bool vsync, bool fps)
        : parts{.gpu = choice("OpenGL (the graphics card)", {a, config::renderer::opengl{}}, icon::none{}, false),
                .cpu = choice("Software (the processor)", {a, config::renderer::software{}}, icon::none{}, false),
                .partial = partial_row("Partial redraw", {a}),
                .flash = flash_row("Flash redrawn areas", {a}),
                .vsync = vsync_row("Vsync", {a}),
                .fps = fps_row("Show frames a second", {a})} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
      parts.note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
      parts.frames_title.apply({.margin = {14.0f, 0.0f, 4.0f, 20.0f}});
      parts.frames_note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
      parts.note.setWrapped(true);
      parts.frames_note.setWrapped(true);
      parts.partial.parts.toggle.setOnNow(partial);
      parts.flash.parts.toggle.setOnNow(flash);
      parts.vsync.parts.toggle.setOnNow(vsync);
      parts.fps.parts.toggle.setOnNow(fps);
    }
  };
  struct parts_t {
    header_t header;
    body list;  // in the settings' own scroll view
  } parts;

  rendering_page(Actions* a, const config::renderer_t& renderer, bool partial = false, bool flash = false,
                 bool vsync = true, bool fps = false)
      : parts{.header = header_t("Rendering", {a}, {a}, true, true),
              .list = body(a, partial, flash, vsync, fps)} {
    fState.apply({.fill = true});
    parts.list.apply({.fillX = true});
    this->show(renderer);
  }
  [[nodiscard]] body& content() { return parts.list; }
  void show_frames(bool partial, bool flash, bool vsync, bool fps) {
    auto& rows = this->content().parts;
    rows.partial.parts.toggle.setOn(partial);
    rows.flash.parts.toggle.setOn(flash);
    rows.vsync.parts.toggle.setOn(vsync);
    rows.fps.parts.toggle.setOn(fps);
  }
  void show(const config::renderer_t& renderer) {
    auto& rows = this->content().parts;
    rows.gpu.set_chosen(renderer == config::renderer_t{config::renderer::opengl{}});
    rows.cpu.set_chosen(renderer == config::renderer_t{config::renderer::software{}});
  }
  void show_motion(std::string_view) {}
  void show_receipts(bool) {}
};

}  // namespace mux::ui
