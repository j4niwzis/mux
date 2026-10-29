// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.display.ui -- Settings' Animations page -- how much the window
// moves -- and its Rendering page -- what draws it, its frames' switches --
// linked from Settings' home (contract::settings_links, settings_pages).
module;
#include "skiff/named.h"
export module mux.plugin.display.ui;

import std;
import skiff.scene;
import skiff.compose;
import skiff.model;
import skiff.widgets.model;
import skiff.nodes.text;
import mux.core;
import mux.config;
import mux.composition;
import mux.ui.base;
import mux.plugin.display.types;

export namespace mux::plugins::display {

using namespace ::mux::ui;

inline auto motion_settings_view(const palette& colours) {
  using field = skiff::model::Field<&config::look_settings::motion>;
  return SKIFF_NAMED(motion_settings_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      wrapped(skiff::compose::styled({.fillX = true, .margin = {4.0f, 20.0f, 12.0f, 20.0f}},
          note_text(colours, "How much the window moves. Reduced keeps the small movements, such as a section unfolding, and shows panels at once."))),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "Full", config::motion::full{})),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "Reduced", config::motion::reduced{})),
      skiff::compose::bound<field>(widgets::ChoiceRowField<config::motion_t>(colours.widgets, "None", config::motion::none{}))));
}
inline auto animations_view(const palette& colours) {
  return SKIFF_NAMED(animations_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar},
          page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Animations", {}, {}, true, true)),
      motion_settings_view(colours)));
}

// Settings' Rendering page: what draws the window, from the next start.
// The Rendering page's frames: a toggle bound to each of frame_settings'
// fields, in effect at once -- the host reads them as it draws.
inline auto frame_settings_view(const palette& colours) {
  using frames = config::frame_settings;
  return SKIFF_NAMED(frame_settings_view, skiff::compose::column(
      skiff::compose::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
      spaced_title(colours, "FRAMES"), setting_switch<&frames::partial_redraw>(colours, "Partial redraw"),
      setting_switch<&frames::flash_redraws>(colours, "Flash redrawn areas"), setting_switch<&frames::vsync>(colours, "Vsync"),
      setting_switch<&frames::show_fps>(colours, "Show frames a second"),
      spaced_note(colours, "Partial redraw repaints only what changed, into a frame kept between them; a part "
                              "that forgets to say it changed then stays as it was. Flashing outlines what each "
                              "frame repainted. Vsync shows frames in step with the screen; off, they are shown as "
                              "soon as drawn. The counter shows frames a second and the last frame's time. All take "
                              "effect at once.")));
}
inline auto rendering_view(const palette& colours) {
  using header = page_header_t<sends<request::settings_home>, sends<request::close_settings>>;
  using field = skiff::model::Field<&config::look_settings::renderer>;
  return SKIFF_NAMED(rendering_view, skiff::compose::column(
      skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      skiff::compose::styled({.depth = 1.0f, .background = colours.sidebar}, page_header<sends<request::settings_home>, sends<request::close_settings>>(colours, "Rendering", {}, {}, true, true)),
      skiff::compose::column(
          skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}),
          skiff::compose::bound<field>(widgets::ChoiceRowField<config::renderer_t>(colours.widgets, "OpenGL (the graphics card)", config::renderer::opengl{})),
          skiff::compose::bound<field>(widgets::ChoiceRowField<config::renderer_t>(colours.widgets, "Software (the processor)", config::renderer::software{})),
          wrapped(skiff::compose::styled({.fillX = true, .margin = {10.0f, 20.0f, 0.0f, 20.0f}}, note_text(colours, "Takes effect when mux starts again.")))),
      frame_settings_view(colours)));
}

// Settings' links to them, and the pages.
inline auto extend(contract::settings_links, type_tag<feature>, const palette& colours) {
  return skiff::compose::column(skiff::compose::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                settings_link(colours, "Animations", icon::motion{}, open_animations{}),
                                settings_link(colours, "Rendering", icon::sliders{}, open_rendering{}));
}
inline auto extend(contract::settings_pages, const ui_needs& n, const animations_page&) { return animations_view(*n.colours); }
inline auto extend(contract::settings_pages, const ui_needs& n, const rendering_page&) { return rendering_view(*n.colours); }

}  // namespace mux::plugins::display
