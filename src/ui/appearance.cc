// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:appearance -- Settings: appearance and rendering.
module;
#include "../inline_in_release.h"
#include "skiff/named.h"
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
import mux.ui.base;
import :accounts;
import :settings_pages;
import :info;
import skiff.widgets.sliderbar;
import skiff.bind;

export namespace mux::ui {

// Theme cards are expressions: the press edits the bound theme, and the
// preview's ring is a projection of that same field.
MUX_INLINE_IN_RELEASE auto theme_card(const palette& colours, config::theme_t theme, std::string label,
                       skia::SkColor back, skia::SkColor incoming, skia::SkColor outgoing) {
  using field = skiff::model::Field<&config::look_settings::theme>;
  return SKIFF_NAMED(theme_card, skiff::compose::bound<field>(skiff::compose::onClick(
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
                                         .width = 44.0f, .height = 14.0f, .cornerRadius = 7.0f}, nodes::Box<>(outgoing))))), label)));
}

// What was picked of a T in a row of nodes that each call one: the press
// answered with it, set in the part the row's field binds.
template <class T>
struct pick {
  using Answer = skiff::bind::Own<skiff::model::SetTo<T>>;
  Answer operator()(const T& one) const { return skiff::bind::own(skiff::model::setTo(one)); }
};

// Theme customization is a local draft. Save writes only overrides for the
// selected base theme; blank fields inherit it and Reset removes its overrides.
struct theme_colour_draft {
  std::string background, sidebar, chosen, text, dim, accent, error;
  std::string selected, selected_text, band, section, tile, sent_time;
  std::string bubble, out_bubble, chat, chat_top, pattern, on_accent;
};
struct theme_colour_field {
  std::string_view label;
  decltype(&config::custom_theme::background) saved;
  decltype(&palette::background) base;
  decltype(&theme_colour_draft::background) draft;
};
inline constexpr std::array<theme_colour_field, 19> theme_colour_fields{{
  {"Window background", &config::custom_theme::background, &palette::background, &theme_colour_draft::background},
  {"Sidebar background", &config::custom_theme::sidebar, &palette::sidebar, &theme_colour_draft::sidebar},
  {"Hover background", &config::custom_theme::chosen, &palette::chosen, &theme_colour_draft::chosen},
  {"Main text", &config::custom_theme::text, &palette::text, &theme_colour_draft::text},
  {"Secondary text", &config::custom_theme::dim, &palette::dim, &theme_colour_draft::dim},
  {"Accent", &config::custom_theme::accent, &palette::accent, &theme_colour_draft::accent},
  {"Error text", &config::custom_theme::error, &palette::error, &theme_colour_draft::error},
  {"Selection background", &config::custom_theme::selected, &palette::selected, &theme_colour_draft::selected},
  {"Selection text", &config::custom_theme::selected_text, &palette::selected_text, &theme_colour_draft::selected_text},
  {"Borders", &config::custom_theme::band, &palette::band, &theme_colour_draft::band},
  {"Section background", &config::custom_theme::section, &palette::section, &theme_colour_draft::section},
  {"Card background", &config::custom_theme::tile, &palette::tile, &theme_colour_draft::tile},
  {"Sent time text", &config::custom_theme::sent_time, &palette::sent_time, &theme_colour_draft::sent_time},
  {"Incoming bubble", &config::custom_theme::bubble, &palette::bubble, &theme_colour_draft::bubble},
  {"Outgoing bubble", &config::custom_theme::out_bubble, &palette::out_bubble, &theme_colour_draft::out_bubble},
  {"Chat background", &config::custom_theme::chat, &palette::chat, &theme_colour_draft::chat},
  {"Chat background top", &config::custom_theme::chat_top, &palette::chat_top, &theme_colour_draft::chat_top},
  {"Wallpaper pattern", &config::custom_theme::pattern, &palette::pattern, &theme_colour_draft::pattern},
  {"Text on accent", &config::custom_theme::on_accent, &palette::on_accent, &theme_colour_draft::on_accent}
}};
[[nodiscard]] inline std::optional<std::uint32_t> theme_colour_of(std::string_view text) {
  if (text.starts_with('#')) text.remove_prefix(1);
  if (text.size() != 6 && text.size() != 8) return std::nullopt;
  std::uint32_t colour = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), colour, 16);
  if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
  return text.size() == 6 ? colour | 0xff000000u : colour;
}
[[nodiscard]] inline std::string theme_colour_word(std::uint32_t colour) {
  return (colour >> 24) == 255 ? std::format("#{:06X}", colour & 0xffffffu) : std::format("#{:08X}", colour);
}
struct theme_details_draft {
  config::theme_t theme;
  bool open = false;
  theme_colour_draft colours;
  std::string corner, font, height, error;
  config::custom_theme saved;
};
struct toggle_theme_details {};
struct save_theme_details {};
struct reset_theme_details {};
struct cancel_theme_details {};
inline void read_theme_measurement(std::string_view text, double least, double most,
    std::string_view name, std::optional<double>& into, std::string& error) {
  if (text.empty()) return;
  double number = 0.0;
  const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (failed != std::errc{} || end != text.data() + text.size() || !std::isfinite(number) || number < least || number > most) {
    if (error.empty()) error = std::format("{} must be between {} and {}.", name, least, most);
  } else into = number;
}
struct theme_details_events {
  auto on(toggle_theme_details, const theme_details_draft& now) const {
    return skiff::model::over<skiff::model::Field<&theme_details_draft::open>>(skiff::model::setTo(!now.open));
  }
  auto on(cancel_theme_details, const theme_details_draft& now) const {
    auto reset = now;
    reset.open = false; reset.error.clear();
    for (const auto& field : theme_colour_fields) {
      const auto saved = now.saved.*field.saved;
      reset.colours.*field.draft = saved ? theme_colour_word(*saved) : std::string();
    }
    reset.corner = now.saved.corner_radius ? std::format("{}", *now.saved.corner_radius) : "";
    reset.font = now.saved.font_size ? std::format("{}", *now.saved.font_size) : "";
    reset.height = now.saved.control_height ? std::format("{}", *now.saved.control_height) : "";
    return skiff::model::over<theme_details_draft>(skiff::model::setTo(std::move(reset)));
  }
  auto on(reset_theme_details, const theme_details_draft& now) const {
    return skiff::model::Up{request::configure_theme{now.theme, std::nullopt}};
  }
  auto on(save_theme_details, const theme_details_draft& now) const {
    config::custom_theme custom;
    std::string error;
    for (const auto& field : theme_colour_fields) {
      const auto& value = now.colours.*field.draft;
      if (value.empty()) continue;
      const auto colour = theme_colour_of(value);
      if (colour) custom.*field.saved = colour;
      else if (error.empty()) error = std::string(field.label) + ": use #RRGGBB or #AARRGGBB.";
    }
    read_theme_measurement(now.corner, 0.0, 32.0, "Corner radius", custom.corner_radius, error);
    read_theme_measurement(now.font, 9.0, 24.0, "Control font size", custom.font_size, error);
    read_theme_measurement(now.height, 24.0, 64.0, "Control height", custom.control_height, error);
    auto asked = error.empty() ? std::optional(request::configure_theme{now.theme, custom}) : std::nullopt;
    return std::tuple{skiff::model::over<skiff::model::Field<&theme_details_draft::error>>(skiff::model::setTo(std::move(error))),
        skiff::model::Up{std::move(asked)}};
  }
};
MUX_INLINE_IN_RELEASE auto theme_details_initial(const config::theme_t& theme,
    const std::map<std::string, config::custom_theme>& overrides) {
  theme_details_draft initial{theme};
  const auto found = overrides.find(std::string(config::word_of(theme)));
  const config::custom_theme custom = found == overrides.end() ? config::custom_theme{} : found->second;
  for (const auto& field : theme_colour_fields) {
    const auto own = custom.*field.saved;
    initial.colours.*field.draft = own ? theme_colour_word(*own) : std::string();
  }
  initial.saved = custom;
  initial.corner = custom.corner_radius ? std::format("{}", *custom.corner_radius) : "";
  initial.font = custom.font_size ? std::format("{}", *custom.font_size) : "";
  initial.height = custom.control_height ? std::format("{}", *custom.control_height) : "";
  return initial;
}
template <std::size_t Index> auto theme_colour_row(const palette& colours, const palette& base) {
  namespace c = skiff::compose;
  constexpr auto member = theme_colour_fields[Index].draft;
  const auto inherited = base.*theme_colour_fields[Index].base;
  auto swatch = c::spec_for<skiff::model::Field<member>>([inherited](const std::string& value) -> scene::Spec {
    return {.background = value.empty() ? inherited : theme_colour_of(value).value_or(inherited)};
  }, c::column(c::vbox(0.0f, {.width = 28.0f, .height = 28.0f, .relativeSize = scene::axes::kNone,
      .alignSelf = scene::align::kMiddle, .cornerRadius = colours.widgets.fCorner,
      .border = scene::Border{colours.band, 1.0f}})));
  return SKIFF_NAMED(theme_colour_row, c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::move(swatch), c::styled({.relativeSize = scene::axes::kNone, .grow = scene::axes::kX},
          model_field<member>(colours, std::string(theme_colour_fields[Index].label), theme_colour_word(inherited)))));
}
template <std::size_t... Index> auto theme_colour_rows(const palette& colours, const palette& base, std::index_sequence<Index...>) {
  // The palette has fixed fields. Bind each directly into the local draft;
  // Each's list walker resolves collections in the outer model instead.
  return SKIFF_NAMED(theme_colour_rows, skiff::compose::column(skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      theme_colour_row<Index>(colours, base)...));
}
MUX_INLINE_IN_RELEASE auto theme_details_view(const palette& colours, const config::theme_t& theme,
    const std::map<std::string, config::custom_theme>& overrides) {
  namespace c = skiff::compose; using draft = theme_details_draft;
  const auto base = palette_of(theme, config::accent::theme_own{}, 100);
  auto fields = theme_colour_rows(colours, base, std::make_index_sequence<theme_colour_fields.size()>{});
  return SKIFF_NAMED(theme_details_view, c::local<draft>(theme_details_events{}, c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY,
      .padding = {8.0f, 20.0f, 16.0f, 20.0f}}),
      c::onClick(toggle_theme_details{}, c::styled({.fillX = true, .height = 36.0f}, widgets::Button<>(colours.widgets, "Configure selected theme"))),
      c::shown_for<skiff::model::Field<&draft::open>>([](bool open) { return open; },
          c::column(c::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {12.0f, 12.0f, 12.0f, 12.0f},
              .cornerRadius = std::max(10.0f, colours.widgets.fCorner), .background = colours.tile}),
              wrapped(nodes::Text("Blank fields keep the theme defaults. Colors accept #RRGGBB or #AARRGGBB (alpha first). Save applies this theme's changes; Reset restores its defaults.", 13.0f, colours.dim)),
              c::text_for<skiff::model::Field<&draft::error>>([](const std::string& error) { return error; }, nodes::Text("", 13.0f, colours.error)),
              std::move(fields),
              model_field<&draft::corner>(colours, "Corner radius (0–32)", "Theme default"),
              model_field<&draft::font>(colours, "Control font size (9–24)", "Theme default"),
              model_field<&draft::height>(colours, "Control height (24–64)", "Theme default"),
              c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                  c::onClick(save_theme_details{}, c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, widgets::Button<>(colours.widgets, "Save"))),
                  c::onClick(reset_theme_details{}, c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, widgets::Button<>(colours.widgets, "Reset theme"))),
                  c::onClick(cancel_theme_details{}, c::styled({.height = 36.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX}, widgets::Button<>(colours.widgets, "Cancel"))))))),
      theme_details_initial(theme, overrides)));
}

// The themes' cards, Telegram's, in its order, with its own pictures'
// colours -- the wallpaper, a bubble received, one sent: bound to the
// theme the model holds, the one in use ringed.
MUX_INLINE_IN_RELEASE auto theme_row(const palette& colours) {
  return SKIFF_NAMED(theme_row, skiff::compose::row(
      skiff::compose::hbox(6.0f, {.autoSize = scene::axes::kBoth}),
      theme_card(colours, config::theme::classic{}, "Classic", skia::colorSetARGB(255, 155, 212, 148),
                 skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 234, 255, 220)),
      theme_card(colours, config::theme::day{}, "Day", skia::colorSetARGB(255, 126, 196, 234),
                 skia::colorSetARGB(255, 255, 255, 255), skia::colorSetARGB(255, 215, 240, 255)),
      theme_card(colours, config::theme::tinted{}, "Tinted", skia::colorSetARGB(255, 72, 87, 97),
                 skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 92, 167, 212)),
      theme_card(colours, config::theme::night{}, "Night", skia::colorSetARGB(255, 72, 87, 97),
                 skia::colorSetARGB(255, 107, 128, 141), skia::colorSetARGB(255, 117, 191, 181)),
      theme_card(colours, config::theme::graphite{}, "Graphite", 0xff262626u, 0xff333333u, 0xff413c65u)));
}
// The themes move sideways in narrow pages; each card binds its own edit.
MUX_INLINE_IN_RELEASE auto theme_field(const palette& colours) {
  return skiff::compose::side_scroll(theme_row(colours));
}
// Telegram's accents, the theme's own first: bound to the accent the model
// holds. Their shades are the theme's: the page is made again with it.
MUX_INLINE_IN_RELEASE auto accent_view(const config::theme_t &theme) {
  using field = skiff::model::Field<&config::look_settings::accent>;
  return accent_circles_for<field>(pick<config::accent_t>{}, theme, true, [](const config::accent_t& now) { return now; });
}
// Values and labels are projected by the model combinators; sliders own
// their input conversion in skiff.widgets.model.
template <auto Member>
MUX_INLINE_IN_RELEASE auto percent_heading(const palette &colours, std::string_view what) {
  return SKIFF_NAMED(percent_heading, skiff::compose::text_for<skiff::model::Field<Member>>(
      [what](int percent) { return std::format("{}: {}%", what, percent); },
      skiff::compose::styled(
          {.margin = {10.0f, 0.0f, 4.0f, 20.0f}},
          nodes::Text(std::string(what), 12.0f, colours.dim))));
}
// How round avatars are, in steps of 5%, set when let go: each change
// rebuilds the window. Older files have no such setting: unset is round --
// shown so by the projection, the slider's choices the setting's own type.
inline constexpr int kRoundAvatar = 100;
inline std::vector<std::optional<int>> rounding_steps() {
  return std::views::iota(0, kRoundAvatar / 5 + 1) | std::views::transform([](int step) { return std::optional<int>(step * 5); }) | std::ranges::to<std::vector>();
}
template <auto Member>
MUX_INLINE_IN_RELEASE auto rounding_setting(const palette& colours, std::string_view what) {
  using field = skiff::model::Field<Member>;
  return SKIFF_NAMED(rounding_setting, skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::text_for<field>(
          [what](const std::optional<int>& percent) { return std::format("{}: {}%", what, percent.value_or(kRoundAvatar)); },
          skiff::compose::styled({.margin = {10.0f, 0.0f, 4.0f, 20.0f}}, nodes::Text(std::string(what), 12.0f, colours.dim))),
      skiff::compose::projected<field>(
          [](const std::optional<int>& percent) { return std::optional<int>(percent.value_or(kRoundAvatar)); },
          skiff::compose::styled({.margin = {10.0f, 28.0f, 10.0f, 28.0f}},
                                 widgets::ChoiceSliderField<std::optional<int>>(colours.widgets, rounding_steps())))));
}
MUX_INLINE_IN_RELEASE auto avatar_rounding_view(const palette& colours) {
  using looks = config::look_settings;
  return SKIFF_NAMED(avatar_rounding_view, skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "AVATARS"),
      rounding_setting<&looks::chat_avatar_rounding>(colours, "Chats"),
      rounding_setting<&looks::person_avatar_rounding>(colours, "People"),
      spaced_note(colours, "0% is square, 100% is round.")));
}

// Settings' Appearance page, as Telegram's: the themes as cards, and the
// accents as circles -- either changes at once -- every chat's looks, the
// space bars, the window's background, blur, scale and opacity. Each made
// of the model's widgets, bound to its setting; but the looks at every
// level and where items are put on the bars, which are their own.
MUX_INLINE_IN_RELEASE auto appearance_settings_view(const palette& colours, const config::theme_t& theme) {
  using looks = config::look_settings;
  auto themes = skiff::compose::styled(
      {.fillX = true,
       .autoSize = scene::axes::kY,
       .margin = {4.0f, 16.0f, 8.0f, 16.0f}},
      theme_field(colours));
  return SKIFF_NAMED(appearance_settings_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "THEME"), std::move(themes),
      spaced_title(colours, "ACCENT"), accent_view(theme), avatar_rounding_view(colours)));
}
MUX_INLINE_IN_RELEASE auto window_settings_view(const palette& colours) {
  using looks = config::look_settings;
  return SKIFF_NAMED(window_settings_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      spaced_title(colours, "LAYOUT"),
      skiff::compose::bound<skiff::model::Field<&looks::layout>>(
          widgets::ChoiceRowField<config::layout_t>(colours.widgets, "Automatic", config::layout::automatic{})),
      skiff::compose::bound<skiff::model::Field<&looks::layout>>(
          widgets::ChoiceRowField<config::layout_t>(colours.widgets, "Portrait: one thing at a time", config::layout::portrait{})),
      skiff::compose::bound<skiff::model::Field<&looks::layout>>(
          widgets::ChoiceRowField<config::layout_t>(colours.widgets, "Landscape: side by side", config::layout::landscape{})),
      spaced_note(colours, "Automatic follows the window: one thing at a time where it is narrow and taller than wide.")));
}
MUX_INLINE_IN_RELEASE auto frame_look_view(const palette& colours, bool see_through) {
  using looks = config::look_settings;
  return SKIFF_NAMED(frame_look_view, skiff::compose::column(
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
                                 "changes here apply at once.")));
}
// Enter in a message: as the input is (a new line by touch, a send with a
// keyboard), or always one of them.
MUX_INLINE_IN_RELEASE auto enter_settings_view(const palette& colours) {
  using field = skiff::model::Field<&config::look_settings::enter>;
  namespace c = skiff::compose;
  return SKIFF_NAMED(enter_settings_view, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
      spaced_title(colours, "ENTER IN A MESSAGE"),
      c::bound<field>(widgets::ChoiceRowField<config::enter_t>(colours.widgets, "As the keyboard is", config::enter_key::automatic{})),
      c::bound<field>(widgets::ChoiceRowField<config::enter_t>(colours.widgets, "Sends", config::enter_key::sends{})),
      c::bound<field>(widgets::ChoiceRowField<config::enter_t>(colours.widgets, "New line", config::enter_key::new_line{})),
      spaced_note(colours, "As the keyboard is: a new line when typing by touch, where the send button sends; a send with a "
                           "keyboard and a mouse. Ctrl+Enter sends, and Shift+Enter is a new line, whatever is chosen.")));
}
// The Appearance page: its sections, each held where it was made. Building
// the whole expression by value copied the nested controls through several
// argument packs, and Android's UI thread has a much smaller stack than a
// desktop's: each section is made into a box of its own on the heap.
template <class Section> [[nodiscard]] auto held(Section section) { return std::make_unique<Section>(std::move(section)); }
MUX_INLINE_IN_RELEASE auto appearance_page(const ui_needs& needs, const config::theme_t& theme, const config::accent_t&) {
  namespace c = skiff::compose;
  const palette& colours = *needs.colours;
  auto looks = held(level_section_plugins::made(colours, choice_level::everywhere{}, *needs.shared));
  looks->apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
  return c::Box(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.depth = 1.0f, .background = colours.sidebar},
                page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Appearance", {}, {}, true, true)),
      held(appearance_settings_view(colours, theme)),
      held(theme_details_view(colours, theme, needs.looks->theme_overrides)),
      held(c::scoped<config::look_settings>(c::handlers(), style_controls(colours, false))),
      std::move(looks),
      held(window_settings_view(colours)),
      held(frame_look_view(colours, needs.looks->window.see_through)),
      held(enter_settings_view(colours)));
}
using appearance_page_t = decltype(appearance_page(std::declval<const ui_needs&>(), config::theme_t{}, config::accent_t{}));


}  // namespace mux::ui
