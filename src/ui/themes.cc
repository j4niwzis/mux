// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:themes -- The themes: what each sets.
export module mux.ui:themes;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
export import :controls;

export namespace mux::ui {

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
  const bool light = std::visit([](auto one) { return one.light; }, chosen);
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

}  // namespace mux::ui
