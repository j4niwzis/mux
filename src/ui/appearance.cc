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
import skiff.model;
import skiff.compose;
import skiff.widgets.model;
import mux.core;
import mux.config;
import :base;
import :icons;
import :controls;
import :themes;
import :accounts;
import :proxies;
import :info;
import :storage;
import skiff.widgets.sliderbar;
import skiff.bind;

export namespace mux::ui {

// A renderer chosen on the Rendering page.
template <class Actions>
struct choose_renderer {
  Actions* actions = nullptr;
  config::renderer_t renderer;
  void operator()() const { actions->set_renderer(renderer); }
};

// A theme's card on the Appearance page: a small picture of it -- its
// background, a bubble of each side -- its name, and a ring where it is the
// one in use. Its colours are its own, not the theme up.
template <class Choose>
struct theme_card : nodes::Stack {
  Choose choose;
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
  theme_card(const palette& colours, Choose what, config::theme_t which, std::string label, skia::SkColor b, skia::SkColor in,
             skia::SkColor out)
      : choose(std::move(what)), theme(which), back(b), bubble(in), mine(out),
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
    choose(theme);
    return true;
  }
};


// What was picked of a T in a row of nodes that each call one: kept until
// it is taken, as a model's widget keeps what it did.
template <class T>
struct pick {
  std::optional<T> value;
  void operator()(const T& one) {
    value = one;
    ++skiff::bind::pendingCount();
  }
};
// Where a slider was let go, kept until it is taken.
struct let_go {
  std::optional<float> fraction;
  void operator()(float at) {
    fraction = at;
    ++skiff::bind::pendingCount();
  }
};

// The themes' cards, Telegram's, in its order, with its own pictures'
// colours -- the wallpaper, a bubble received, one sent: bound to the
// theme the model holds, the one in use ringed.
struct theme_row : nodes::Stack {
  using card = theme_card<pick<config::theme_t>>;
  struct parts_t {
    card classic, day, tinted, night;
  } parts;
  explicit theme_row(const palette& colours)
      : parts{.classic = card(colours, {}, config::theme::classic{}, "Classic", skia::colorSetARGB(255, 155, 212, 148),
                              skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 234, 255, 220)),
              .day = card(colours, {}, config::theme::day{}, "Day", skia::colorSetARGB(255, 126, 196, 234),
                          skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 215, 240, 255)),
              .tinted = card(colours, {}, config::theme::tinted{}, "Tinted", skia::colorSetARGB(255, 72, 87, 97),
                             skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 92, 167, 212)),
              .night = card(colours, {}, config::theme::night{}, "Night", skia::colorSetARGB(255, 72, 87, 97),
                            skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 117, 191, 181))} {
    this->setHorizontal();
    this->setGap(6.0f);
    fState.apply({.autoSize = scene::axes::kBoth});
  }
};
// Moved along where the page is narrower than they are -- a phone's --
// rather than cut off at its edge.
struct theme_field : side_scroll<theme_row> {
  explicit theme_field(const palette& colours) : side_scroll<theme_row>(theme_row(colours)) {}
  void read(const config::theme_t& now) {
    auto& [classic, day, tinted, night] = parts.line.parts;
    for (auto* card : {&classic, &day, &tinted, &night})
      card->set_chosen(card->theme == now);
    this->markDamaged();
  }
  std::vector<skiff::model::SetTo<config::theme_t>> takeChanges() {
    std::vector<skiff::model::SetTo<config::theme_t>> out;
    auto& [classic, day, tinted, night] = parts.line.parts;
    for (auto* card : {&classic, &day, &tinted, &night})
      if (auto picked = std::exchange(card->choose.value, std::nullopt))
        out.push_back(skiff::model::setTo(*picked));
    return out;
  }
};
// Telegram's accents, the theme's own first: bound to the accent the model
// holds. Their shades are the theme's: the page is made again with it.
struct accent_field : accent_circles<pick<config::accent_t>> {
  explicit accent_field(const config::theme_t& in) : accent_circles<pick<config::accent_t>>({}, in, true) {}
  void read(const config::accent_t& now) { this->show_chosen(now); }
  std::vector<skiff::model::SetTo<config::accent_t>> takeChanges() {
    std::vector<skiff::model::SetTo<config::accent_t>> out;
    for (auto& circle : parts.circles)
      if (auto picked = std::exchange(circle.choose.value, std::nullopt))
        out.push_back(skiff::model::setTo(*picked));
    return out;
  }
};
// A slider over an int between its ends, set where it is let go: the
// window's opacity, 20% to 100%; or the nearest of a list of them -- the
// interface's scale, among kScales.
struct opacity_field : widgets::internal::SliderBar<scene::NoAction, let_go> {
  explicit opacity_field(const palette& colours)
      : widgets::internal::SliderBar<scene::NoAction, let_go>(colours.widgets, {}, {}) {
    this->apply({.margin = {10.0f, 28.0f, 10.0f, 28.0f}});
  }
  void read(int percent) { this->setFraction(static_cast<float>(percent - 20) / 80.0f); }
  std::vector<skiff::model::SetTo<int>> takeChanges() {
    std::vector<skiff::model::SetTo<int>> out;
    if (auto at = std::exchange(this->onDone().fraction, std::nullopt))
      out.push_back(skiff::model::setTo(static_cast<int>(std::lround(20.0f + std::clamp(*at, 0.0f, 1.0f) * 80.0f))));
    return out;
  }
};
struct scale_field : widgets::internal::SliderBar<scene::NoAction, let_go> {
  explicit scale_field(const palette& colours)
      : widgets::internal::SliderBar<scene::NoAction, let_go>(colours.widgets, {}, {}) {
    this->apply({.margin = {10.0f, 28.0f, 10.0f, 28.0f}});
  }
  void read(int percent) {
    const auto chosen = std::ranges::find(kScales, percent);
    this->setFraction(chosen == kScales.end()
                          ? 0.0f
                          : static_cast<float>(chosen - kScales.begin()) / static_cast<float>(kScales.size() - 1));
  }
  std::vector<skiff::model::SetTo<int>> takeChanges() {
    std::vector<skiff::model::SetTo<int>> out;
    if (auto at = std::exchange(this->onDone().fraction, std::nullopt)) {
      const auto last = static_cast<float>(kScales.size() - 1);
      out.push_back(skiff::model::setTo(kScales[static_cast<std::size_t>(std::lround(std::clamp(*at, 0.0f, 1.0f) * last))]));
    }
    return out;
  }
};
// A section's title saying a setting's value: the scale, the opacity.
struct percent_title : nodes::Text {
  std::string_view what;
  percent_title(const palette& colours, std::string_view said) : nodes::Text(std::string(said), 12.0f, colours.dim), what(said) {
    this->apply({.margin = {10.0f, 0.0f, 4.0f, 20.0f}});
  }
  void read(int percent) { this->setText(std::format("{}: {}%", what, percent)); }
};
// Home without direct messages: only where Home is without what spaces
// hold, so it reads both, and is flipped only then.
struct home_direct_switch : nodes::Stack {
  struct parts_t {
    nodes::Text label;
    widgets::internal::Toggle<widgets::Pressed> toggle;
  } parts;
  config::look_settings shown;
  explicit home_direct_switch(const palette& colours)
      : parts{.label = nodes::Text("And without direct messages", 15.0f, colours.text),
              .toggle = widgets::internal::Toggle<widgets::Pressed>(colours.widgets, widgets::Pressed{})} {
    this->setHorizontal();
    this->setGap(16.0f);
    fState.apply({.fillX = true, .height = row_item<nothing>::kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}});
    parts.label.setElided(true);
    parts.label.apply({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle});
    parts.toggle.apply({.alignSelf = scene::align::kMiddle});
  }
  void read(const config::look_settings& now) {
    shown = now;
    parts.toggle.setOn(now.home_hides_direct);
    this->apply({.alpha = now.home_hides_spaced ? 1.0f : 0.4f, .disabled = !now.home_hides_spaced});
  }
  std::vector<skiff::model::SetTo<config::look_settings>> takeChanges() {
    std::vector<skiff::model::SetTo<config::look_settings>> out;
    if (std::exchange(parts.toggle.onToggle().fCount, 0) % 2 == 1 && shown.home_hides_spaced) {
      auto next = shown;
      next.home_hides_direct = !next.home_hides_direct;
      out.push_back(skiff::model::setTo(std::move(next)));
    }
    return out;
  }
};

// Settings' Appearance page, as Telegram's: the themes as cards, and the
// accents as circles -- either changes at once -- every chat's looks, the
// space bars, the window's background, blur, scale and opacity. Each made
// of the model's widgets, bound to its setting; but the looks at every
// level and where items are put on the bars, which are their own.
inline auto appearance_settings_view(const palette& colours, const config::theme_t& theme) {
  using looks = config::look_settings;
  auto themes = skiff::compose::bound<skiff::model::Field<&looks::theme>>(theme_field(colours));
  themes.apply({.fillX = true, .autoSize = scene::axes::kY, .margin = {4.0f, 16.0f, 8.0f, 16.0f}});
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), spaced_title(colours, "THEME"),
      std::move(themes), spaced_title(colours, "ACCENT"),
      skiff::compose::bound<skiff::model::Field<&looks::accent>>(accent_field(theme)));
}
inline auto window_settings_view(const palette& colours) {
  using looks = config::look_settings;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), spaced_title(colours, "SPACES"),
      setting_switch<&looks::spaces>(colours, "Space bars"), setting_switch<&looks::top_bar>(colours, "The bar after \"mux\""),
      setting_switch<&looks::home_hides_spaced>(colours, "Home without chats spaces hold (not direct messages)"),
      skiff::compose::bound<looks>(home_direct_switch(colours)));
}
inline auto frame_look_view(const palette& colours, bool see_through) {
  using looks = config::look_settings;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      setting_switch<&looks::wallpaper_behind>(colours, "Background behind the whole window"),
      setting_switch<&looks::live_blur>(colours, "Frosted menus blur what is under them (live)"),
      skiff::compose::bound<skiff::model::Field<&looks::interface_scale>>(percent_title(colours, "INTERFACE SCALE")),
      skiff::compose::bound<skiff::model::Field<&looks::interface_scale>>(scale_field(colours)),
      skiff::compose::bound<skiff::model::Field<&looks::window_opacity>>(percent_title(colours, "WINDOW OPACITY")),
      skiff::compose::bound<skiff::model::Field<&looks::window_opacity>>(opacity_field(colours)),
      spaced_note(colours, see_through ? "The panels at this opacity, and what is under the window through them."
                                       : "Below 100% the window shows what is under it, where a compositor (picom, KWin, "
                                         "Mutter) blends windows. Made see-through when mux starts again; from then on, "
                                         "changes here apply at once."));
}
template <class Actions>
struct appearance_page : nodes::Stack {
  using header_t = page_header<sends<::mux::ui::request::settings_home>, sends<::mux::ui::request::close_settings>>;
  using settings_t = decltype(skiff::compose::column(
      skiff::compose::vbox(), appearance_settings_view(std::declval<const palette&>(), std::declval<const config::theme_t&>()),
      std::declval<look_choices<Actions>>(), window_settings_view(std::declval<const palette&>()),
      std::declval<spaces_choices<Actions>>(), frame_look_view(std::declval<const palette&>(), false)));
  struct parts_t {
    header_t header;
    settings_t settings;
  } parts;

  appearance_page(const ui_needs<Actions>& n, const config::theme_t& theme, const config::accent_t&)
      : appearance_page(*n.colours, *n.looks, *n.shared, n.actions, theme) {}
  appearance_page(const palette& colours, const looks_shown& looks, const ui_shared& shared, Actions* a, const config::theme_t& theme)
      : parts{.header = header_t(colours, "Appearance", {a}, {a}, true, true),
              .settings = skiff::compose::column(
                  skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), appearance_settings_view(colours, theme),
                  look_choices<Actions>(a, colours, looks, choice_level::everywhere{}), window_settings_view(colours),
                  spaces_choices<Actions>(a, colours, shared), frame_look_view(colours, looks.window.see_through))} {
    fState.apply({.fill = true});
    std::get<1>(parts.settings.fParts).apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    std::get<3>(parts.settings.fParts).setVisible(looks.window.spaces);
  }
  void show_receipts(bool) {}
};

// Settings' Rendering page: what draws the window, from the next start.
// The Rendering page's frames: a toggle bound to each of frame_settings'
// fields, in effect at once -- the host reads them as it draws.
inline auto frame_settings_view(const palette& colours) {
  using frames = config::frame_settings;
  return skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
      spaced_title(colours, "FRAMES"), setting_switch<&frames::partial_redraw>(colours, "Partial redraw"),
      setting_switch<&frames::flash_redraws>(colours, "Flash redrawn areas"), setting_switch<&frames::vsync>(colours, "Vsync"),
      setting_switch<&frames::show_fps>(colours, "Show frames a second"),
      spaced_note(colours, "Partial redraw repaints only what changed, into a frame kept between them; a part "
                              "that forgets to say it changed then stays as it was. Flashing outlines what each "
                              "frame repainted. Vsync shows frames in step with the screen; off, they are shown as "
                              "soon as drawn. The counter shows frames a second and the last frame's time. All take "
                              "effect at once."));
}
template <class Actions>
struct rendering_page : nodes::Stack {
  using header_t = page_header<sends<::mux::ui::request::settings_home>, sends<::mux::ui::request::close_settings>>;
  using choice = row_item<choose_renderer<Actions>>;
  // What is under the header: it scrolls where the dialog is too low for it.
  struct body : nodes::Stack {
    struct parts_t {
      choice gpu;
      choice cpu;
      nodes::Text note;
    } parts;
    body(const palette& colours, Actions* a)
        : parts{.gpu = choice(colours, "OpenGL (the graphics card)", {a, config::renderer::opengl{}}, icon::none{}, false),
                .cpu = choice(colours, "Software (the processor)", {a, config::renderer::software{}}, icon::none{}, false),
                .note = note_text(colours, "Takes effect when mux starts again.")} {
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}});
      parts.note.apply({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}});
      parts.note.setWrapped(true);
    }
  };
  using settings_t = decltype(frame_settings_view(std::declval<const palette&>()));
  struct parts_t {
    header_t header;
    body list;  // in the settings' own scroll view
    settings_t settings;
  } parts;

  rendering_page(const ui_needs<Actions>& n, const config::renderer_t& renderer)
      : rendering_page(*n.colours, n.actions, renderer) {}
  rendering_page(const palette& colours, Actions* a, const config::renderer_t& renderer)
      : parts{.header = header_t(colours, "Rendering", {a}, {a}, true, true),
              .list = body(colours, a),
              .settings = frame_settings_view(colours)} {
    fState.apply({.fill = true});
    parts.list.apply({.fillX = true});
    this->show(renderer);
  }
  [[nodiscard]] body& content() { return parts.list; }
  void show(const config::renderer_t& renderer) {
    auto& rows = this->content().parts;
    rows.gpu.set_chosen(renderer == config::renderer_t{config::renderer::opengl{}});
    rows.cpu.set_chosen(renderer == config::renderer_t{config::renderer::software{}});
  }
  void show_receipts(bool) {}
};

}  // namespace mux::ui
