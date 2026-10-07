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
import :info;
import skiff.widgets.sliderbar;

export namespace mux::ui {

// A theme chosen on the Appearance page, a renderer on the Rendering page.
template <class Actions>
struct choose_theme {
  Actions* actions = nullptr;
  config::theme_t theme;
  void operator()() const { actions->set_theme(theme); }
};
// Home without what spaces hold, for every account: flipped.
template <class Actions>
struct flip_home_hides {
  Actions* actions = nullptr;
  const looks_shown* looks = nullptr;
  void operator()() const { actions->set_home_hides(choice_level::everywhere{}, !looks->window.home_hides); }
};
template <class Actions>
struct flip_home_direct {
  Actions* actions = nullptr;
  const looks_shown* looks = nullptr;
  void operator()() const {
    if (looks->window.home_hides)
      actions->set_home_direct(choice_level::everywhere{}, !looks->window.home_direct);
  }
};
// The window's opacity, let go at on its slider: 20% to 100%.
template <class Actions>
struct opacity_done {
  Actions* actions = nullptr;
  void operator()(float fraction) const {
    actions->set_window_opacity(static_cast<int>(std::lround(20.0f + std::clamp(fraction, 0.0f, 1.0f) * 80.0f)));
  }
};
// The interface's scale, let go at on its slider: the nearest of kScales.
template <class Actions>
struct scale_done {
  Actions* actions = nullptr;
  void operator()(float fraction) const {
    const auto last = static_cast<float>(kScales.size() - 1);
    const auto at = static_cast<std::size_t>(std::lround(std::clamp(fraction, 0.0f, 1.0f) * last));
    actions->set_interface_scale(kScales[at]);
  }
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
    // The ring it is lit with as it is the one chosen.
    skia::SkColor ring = 0;
    picture_t(skia::SkColor back, skia::SkColor in, skia::SkColor out, skia::SkColor lit)
        : parts{.incoming = nodes::Box<>(in), .outgoing = nodes::Box<>(out)}, ring(lit) {
      fState.apply({.place = scene::anchor::kTopLeft, .y = -62.0f, .fillX = true, .height = 56.0f, .cornerRadius = 8.0f,
                    .background = back});
      parts.incoming.apply({.place = scene::anchor::kTopLeft, .x = 6.0f, .y = 8.0f, .width = 44.0f, .height = 14.0f,
                            .cornerRadius = 7.0f});
      parts.outgoing.apply({.place = scene::anchor::kTopRight, .x = -6.0f, .y = 30.0f, .width = 44.0f, .height = 14.0f,
                            .cornerRadius = 7.0f});
    }
    void set_ring(bool on) { fState.apply({.border = scene::Border{on ? ring : 0u, on ? 2.0f : 0.0f}}); }
  };
  struct parts_t {
    nodes::Text name;
    picture_t picture;
  } parts;
  theme_card(const palette& colours, Actions* a, config::theme_t which, std::string label, skia::SkColor b, skia::SkColor in,
             skia::SkColor out)
      : actions(a), theme(which), back(b), bubble(in), mine(out),
        parts{.name = nodes::Text(std::move(label), 12.0f, colours.dim), .picture = picture_t(b, in, out, colours.accent)} {
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

// The window's accent: a circle pressed.
template <class Actions>
struct set_accent_to {
  Actions* actions = nullptr;
  void operator()(const config::accent_t& one) const { actions->set_accent(one); }
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
    cards_row(const palette& colours, Actions* a)
        : parts{.classic = card(colours, a, config::theme::classic{}, "Classic", skia::colorSetARGB(255, 155, 212, 148),
                                skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 234, 255, 220)),
                .day = card(colours, a, config::theme::day{}, "Day", skia::colorSetARGB(255, 126, 196, 234),
                            skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 215, 240, 255)),
                .tinted = card(colours, a, config::theme::tinted{}, "Tinted", skia::colorSetARGB(255, 72, 87, 97),
                               skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 92, 167, 212)),
                .night = card(colours, a, config::theme::night{}, "Night", skia::colorSetARGB(255, 72, 87, 97),
                              skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 117, 191, 181))} {
      this->setHorizontal();
      this->setGap(6.0f);
      fState.apply({.autoSize = scene::axes::kBoth});
    }
  };
  // The cards, moved along where the page is narrower than they are -- a
  // phone's -- rather than cut off at its edge.
  using cards_view = side_scroll<cards_row>;
  using circles_row = accent_circles<set_accent_to<Actions>>;
  struct parts_t {
    header_t header;
    nodes::Text theme_title;
    cards_view cards;
    nodes::Text accent_title;
    circles_row circles;
    // Every chat's background, bubbles and panels, here -- not in a dialog.
    look_choices<Actions> looks;
    nodes::Text spaces_title;
    switch_row<ask<Actions, &Actions::flip_spaces>> spaces;
    switch_row<ask<Actions, &Actions::flip_top_bar>> top_bar;
    switch_row<flip_home_hides<Actions>> home_hides;
    switch_row<flip_home_direct<Actions>> home_direct;
    spaces_choices<Actions> places;
    switch_row<ask<Actions, &Actions::flip_wallpaper_behind>> behind;
    switch_row<ask<Actions, &Actions::flip_live_blur>> live_blur;
    nodes::Text scale_title;
    widgets::SliderBar<scene::NoAction, scale_done<Actions>> scale;
    nodes::Text window_title;
    widgets::SliderBar<scene::NoAction, opacity_done<Actions>> opacity;
    nodes::Text window_note;
  } parts;

  appearance_page(const ui_needs<Actions>& n, const config::theme_t& theme, const config::accent_t& accent)
      : appearance_page(*n.colours, *n.looks, *n.shared, n.actions, theme, accent) {}
  appearance_page(const palette& colours, const looks_shown& looks, const ui_shared& shared, Actions* a, const config::theme_t& theme, const config::accent_t& accent)
      : parts{.header = header_t(colours, "Appearance", {a}, {a}, true, true),
              .theme_title = section_title(colours, "THEME"),
              .cards = cards_view(cards_row(colours, a)),
              .accent_title = section_title(colours, "ACCENT"),
              .circles = circles_row({a}, theme, true),
              .looks = look_choices<Actions>(a, colours, looks, choice_level::everywhere{}),
              .spaces_title = section_title(colours, "SPACES"),
              .spaces = switch_row<ask<Actions, &Actions::flip_spaces>>(colours, "Space bars", {a}),
              .top_bar = switch_row<ask<Actions, &Actions::flip_top_bar>>(colours, "The bar after \"mux\"", {a}),
              .home_hides = switch_row<flip_home_hides<Actions>>(colours, "Home without chats spaces hold (not direct messages)", {a, &looks}),
              .home_direct = switch_row<flip_home_direct<Actions>>(colours, "And without direct messages", {a, &looks}),
              .places = spaces_choices<Actions>(a, colours, shared),
              .behind = switch_row<ask<Actions, &Actions::flip_wallpaper_behind>>(colours, "Background behind the whole window", {a}),
              .live_blur = switch_row<ask<Actions, &Actions::flip_live_blur>>(colours, "Frosted menus blur what is under them (live)", {a}),
              .scale_title = section_title(colours, std::format("INTERFACE SCALE: {}%", looks.window.interface_scale)),
              .scale = widgets::SliderBar<scene::NoAction, scale_done<Actions>>(colours.widgets, {}, {a}),
              .window_title = section_title(colours, std::format("WINDOW OPACITY: {}%", looks.window.chosen)),
              .opacity = widgets::SliderBar<scene::NoAction, opacity_done<Actions>>(colours.widgets, {}, {a}),
              .window_note = note_text(colours, looks.window.see_through
                                ? "The panels at this opacity, and what is under the window through them."
                                : "Below 100% the window shows what is under it, where a compositor (picom, KWin, "
                                  "Mutter) blends windows. Made see-through when mux starts again; from then on, "
                                  "changes here apply at once.")} {
    fState.apply({.fill = true});
    parts.theme_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    parts.cards.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}});
    parts.accent_title.apply({.margin = {6.0f, 0.0f, 4.0f, 20.0f}});
    parts.looks.apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    parts.spaces_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
    parts.spaces.parts.toggle.setOnNow(looks.window.spaces);
    parts.top_bar.parts.toggle.setOnNow(looks.window.top_bar);
    parts.home_hides.parts.toggle.setOnNow(looks.window.home_hides);
    parts.home_direct.parts.toggle.setOnNow(looks.window.home_direct);
    // Only where Home is without what spaces hold.
    if (!looks.window.home_hides)
      parts.home_direct.apply({.alpha = 0.4f, .disabled = true});
    parts.places.setVisible(looks.window.spaces);
    parts.behind.parts.toggle.setOnNow(looks.window.behind);
    parts.live_blur.parts.toggle.setOnNow(looks.window.live_blur);
    parts.scale_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
    // Where the scale up is among those one can choose.
    const auto chosen_scale = std::ranges::find(kScales, looks.window.interface_scale);
    parts.scale.setFraction(chosen_scale == kScales.end()
                                ? 0.0f
                                : static_cast<float>(chosen_scale - kScales.begin()) / static_cast<float>(kScales.size() - 1));
    parts.scale.apply({.margin = {10.0f, 28.0f, 10.0f, 28.0f}});
    parts.window_title.apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
    parts.opacity.setFraction(static_cast<float>(looks.window.chosen - 20) / 80.0f);
    parts.opacity.apply({.margin = {10.0f, 28.0f, 10.0f, 28.0f}});
    parts.window_note.apply({.fillX = true, .margin = {0.0f, 20.0f, 0.0f, 20.0f}});
    parts.window_note.setWrapped(true);
    this->show(theme, accent);
  }
  void show(const config::theme_t& theme, const config::accent_t& accent) {
    auto& [classic, day, tinted, night] = parts.cards.parts.line.parts;
    for (auto* card : {&classic, &day, &tinted, &night})
      card->set_chosen(card->theme == theme);
    parts.circles.show_chosen(accent);
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
      nodes::Text note;
      nodes::Text frames_title;
      partial_row partial;
      flash_row flash;
      vsync_row vsync;
      fps_row fps;
      nodes::Text frames_note;
    } parts;
    body(const palette& colours, Actions* a, bool partial, bool flash, bool vsync, bool fps)
        : parts{.gpu = choice(colours, "OpenGL (the graphics card)", {a, config::renderer::opengl{}}, icon::none{}, false),
                .cpu = choice(colours, "Software (the processor)", {a, config::renderer::software{}}, icon::none{}, false),
                .note = note_text(colours, "Takes effect when mux starts again."),
                .frames_title = section_title(colours, "FRAMES"),
                .partial = partial_row(colours, "Partial redraw", {a}),
                .flash = flash_row(colours, "Flash redrawn areas", {a}),
                .vsync = vsync_row(colours, "Vsync", {a}),
                .fps = fps_row(colours, "Show frames a second", {a}),
                .frames_note = note_text(colours, "Partial redraw repaints only what changed, into a frame kept between them; a part "
                              "that forgets to say it changed then stays as it was. Flashing outlines what each "
                              "frame repainted. Vsync shows frames in step with the screen; off, they are shown as "
                              "soon as drawn. The counter shows frames a second and the last frame's time. All take "
                              "effect at once.")} {
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

  rendering_page(const ui_needs<Actions>& n, const config::renderer_t& renderer, bool partial = false, bool flash = false,
                 bool vsync = true, bool fps = false)
      : rendering_page(*n.colours, n.actions, renderer, partial, flash, vsync, fps) {}
  rendering_page(const palette& colours, Actions* a, const config::renderer_t& renderer, bool partial, bool flash, bool vsync,
                 bool fps)
      : parts{.header = header_t(colours, "Rendering", {a}, {a}, true, true),
              .list = body(colours, a, partial, flash, vsync, fps)} {
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
