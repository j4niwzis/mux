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

// Theme cards are expressions: the press edits the bound theme, and the
// preview's ring is a projection of that same field.
inline auto theme_card(const palette& colours, config::theme_t theme, std::string label,
                       skia::SkColor back, skia::SkColor incoming, skia::SkColor outgoing) {
  using field = skiff::model::Field<&config::look_settings::theme>;
  return skiff::compose::bound<field>(skiff::compose::onClick(
      skiff::bind::own(skiff::model::setTo(theme)),
      skiff::compose::column(
          skiff::compose::vbox(0.0f, {.width = 92.0f, .height = 92.0f, .padding = {66.0f, 6.0f, 0.0f, 6.0f}}),
          skiff::compose::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(label, 12.0f, colours.dim)),
          skiff::compose::spec_for<field>(
              [theme, ring = colours.accent](const config::theme_t& now) -> scene::Spec {
                const bool selected = theme == now;
                return {.border = scene::Border{selected ? ring : 0u, selected ? 2.0f : 0.0f}};
              },
              skiff::compose::column(
                  skiff::compose::vbox(0.0f, {.place = scene::anchor::kTopLeft, .y = -62.0f, .fillX = true,
                                             .height = 56.0f, .cornerRadius = 8.0f, .background = back}),
                  skiff::compose::styled({.place = scene::anchor::kTopLeft, .x = 6.0f, .y = 8.0f,
                                         .width = 44.0f, .height = 14.0f, .cornerRadius = 7.0f}, nodes::Box<>(incoming)),
                  skiff::compose::styled({.place = scene::anchor::kTopRight, .x = -6.0f, .y = 30.0f,
                                         .width = 44.0f, .height = 14.0f, .cornerRadius = 7.0f}, nodes::Box<>(outgoing))))), label));
}

// What was picked of a T in a row of nodes that each call one: the press
// answered with it, set in the part the row's field binds.
template <class T>
struct pick {
  using Answer = skiff::bind::Own<skiff::model::SetTo<T>>;
  Answer operator()(const T& one) const { return skiff::bind::own(skiff::model::setTo(one)); }
};

// The themes' cards, Telegram's, in its order, with its own pictures'
// colours -- the wallpaper, a bubble received, one sent: bound to the
// theme the model holds, the one in use ringed.
inline auto theme_row(const palette& colours) {
  return skiff::compose::row(
      skiff::compose::hbox(6.0f, {.autoSize = scene::axes::kBoth}),
      theme_card(colours, config::theme::classic{}, "Classic", skia::colorSetARGB(255, 155, 212, 148),
                 skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 234, 255, 220)),
      theme_card(colours, config::theme::day{}, "Day", skia::colorSetARGB(255, 126, 196, 234),
                 skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 215, 240, 255)),
      theme_card(colours, config::theme::tinted{}, "Tinted", skia::colorSetARGB(255, 72, 87, 97),
                 skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 92, 167, 212)),
      theme_card(colours, config::theme::night{}, "Night", skia::colorSetARGB(255, 72, 87, 97),
                 skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 117, 191, 181)));
}
// The themes move sideways in narrow pages; each card binds its own edit.
inline auto theme_field(const palette& colours) {
  return side_scroll<decltype(theme_row(colours))>(theme_row(colours));
}
// Telegram's accents, the theme's own first: bound to the accent the model
// holds. Their shades are the theme's: the page is made again with it.
inline auto accent_view(const config::theme_t &theme) {
  using field = skiff::model::Field<&config::look_settings::accent>;
  auto circles =
      accent_circles<pick<config::accent_t>>::accents_of(true) |
      std::views::transform([&](const config::accent_t &accent) {
        const auto shade = colour_of(accent, theme);
        return skiff::compose::spec_for<field>(
            [accent, shade](const config::accent_t &now) -> scene::Spec {
              const bool chosen = accent == now;
              return {.border = scene::Border{chosen ? shade : 0u,
                                              chosen ? 2.0f : 0.0f}};
            },
            accent_circle<pick<config::accent_t>>({}, accent, theme));
      }) |
      std::ranges::to<std::vector>();
  return skiff::compose::many(
      skiff::compose::hbox(4.0f, {.fillX = true,
                                  .autoSize = scene::axes::kY,
                                  .margin = {4.0f, 16.0f, 8.0f, 16.0f}}),
      std::move(circles));
}
// Values and labels are projected by the model combinators; sliders own
// their input conversion in skiff.widgets.model.
template <auto Member>
inline auto percent_heading(const palette &colours, std::string_view what) {
  return skiff::compose::text_for<skiff::model::Field<Member>>(
      [what](int percent) { return std::format("{}: {}%", what, percent); },
      skiff::compose::styled(
          {.margin = {10.0f, 0.0f, 4.0f, 20.0f}},
          nodes::Text(std::string(what), 12.0f, colours.dim)));
}
// Home without direct messages: only where Home is without what spaces
// hold, so it reads both, and is flipped only then.
inline auto home_direct_switch(const palette& colours) {
  return skiff::compose::row(
      skiff::compose::hbox(16.0f, {.fillX = true, .height = row_item<nothing>::kHeight, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
      skiff::compose::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                             elided(nodes::Text("And without direct messages", 15.0f, colours.text))),
      skiff::compose::bound<skiff::model::Field<&config::look_settings::home_hides_direct>>(
          skiff::compose::styled({.alignSelf = scene::align::kMiddle}, widgets::ToggleField<bool>(colours.widgets))));
}

// Settings' Appearance page, as Telegram's: the themes as cards, and the
// accents as circles -- either changes at once -- every chat's looks, the
// space bars, the window's background, blur, scale and opacity. Each made
// of the model's widgets, bound to its setting; but the looks at every
// level and where items are put on the bars, which are their own.
inline auto appearance_settings_view(const palette& colours, const config::theme_t& theme) {
  using looks = config::look_settings;
  auto themes = skiff::compose::styled(
      {.fillX = true,
       .autoSize = scene::axes::kY,
       .margin = {4.0f, 16.0f, 8.0f, 16.0f}},
      theme_field(colours));
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "THEME"), std::move(themes),
      spaced_title(colours, "ACCENT"), accent_view(theme));
}
inline auto window_settings_view(const palette& colours) {
  using looks = config::look_settings;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "SPACES"),
      setting_switch<&looks::spaces>(colours, "Space bars"),
      setting_switch<&looks::top_bar>(colours, "The bar after \"mux\""),
      setting_switch<&looks::home_hides_spaced>(
          colours, "Home without chats spaces hold (not direct messages)"),
      skiff::compose::spec_for<looks>(
          [](const looks &now) -> scene::Spec {
            return {.alpha = now.home_hides_spaced ? 1.0f : 0.4f,
                    .disabled = !now.home_hides_spaced};
          },
          home_direct_switch(colours)));
}
inline auto frame_look_view(const palette& colours, bool see_through) {
  using looks = config::look_settings;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      setting_switch<&looks::wallpaper_behind>(
          colours, "Background behind the whole window"),
      setting_switch<&looks::live_blur>(
          colours, "Frosted menus blur what is under them (live)"),
      percent_heading<&looks::interface_scale>(colours, "INTERFACE SCALE"),
      skiff::compose::bound<skiff::model::Field<&looks::interface_scale>>(
          skiff::compose::styled(
              {.margin = {10.0f, 28.0f, 10.0f, 28.0f}},
              widgets::ChoiceSliderField<int>(
                  colours.widgets,
                  std::vector<int>(kScales.begin(), kScales.end())))),
      percent_heading<&looks::window_opacity>(colours, "WINDOW OPACITY"),
      skiff::compose::bound<skiff::model::Field<&looks::window_opacity>>(
          skiff::compose::styled(
              {.margin = {10.0f, 28.0f, 10.0f, 28.0f}},
              widgets::SliderField<int>(colours.widgets, 20, 100))),
      spaced_note(colours, see_through
                               ? "The panels at this opacity, and what is "
                                 "under the window through them."
                               : "Below 100% the window shows what is under "
                                 "it, where a compositor (picom, KWin, "
                                 "Mutter) blends windows. Made see-through "
                                 "when mux starts again; from then on, "
                                 "changes here apply at once."));
}
template <class Actions>
auto appearance_page(const ui_needs<Actions>& needs, const config::theme_t& theme, const config::accent_t&) {
  const auto& colours = *needs.colours;
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Appearance", {}, {}, true, true)),
      skiff::compose::column(
          skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          appearance_settings_view(colours, theme),
          skiff::compose::scoped<config::look_settings>(skiff::compose::handlers(), style_controls(colours, false)),
          skiff::compose::styled({.margin = {6.0f, 10.0f, 0.0f, 10.0f}}, look_choices<Actions>(colours, *needs.looks, choice_level::everywhere{})),
          window_settings_view(colours), skiff::compose::visible(needs.looks->window.spaces, spaces_choices<Actions>(colours, *needs.shared)),
          frame_look_view(colours, needs.looks->window.see_through)));
}
template <class Actions>
using appearance_page_t = decltype(appearance_page(std::declval<const ui_needs<Actions>&>(), config::theme_t{}, config::accent_t{}));

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
inline auto rendering_page(const palette& colours) {
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  using field = skiff::model::Field<&config::look_settings::renderer>;
  return skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Rendering", {}, {}, true, true)),
      skiff::compose::column(
          skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
          skiff::compose::bound<field>(widgets::ChoiceRowField<config::renderer_t>(colours.widgets, "OpenGL (the graphics card)", config::renderer::opengl{})),
          skiff::compose::bound<field>(widgets::ChoiceRowField<config::renderer_t>(colours.widgets, "Software (the processor)", config::renderer::software{})),
          wrapped(skiff::compose::styled({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}}, note_text(colours, "Takes effect when mux starts again.")))),
      frame_settings_view(colours));
}
using rendering_page_t = decltype(rendering_page(std::declval<const palette&>()));

}  // namespace mux::ui
