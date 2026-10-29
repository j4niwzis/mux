// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.display.types -- What the display plugin names, and what it
// says it provides: Settings' Animations and Rendering pages, their links,
// and a part of the program that opens them. Light: mux.ui sees it.
export module mux.plugin.display.types;

import std;
import knot;
import mux.core;
import mux.config;
import mux.composition;

export namespace mux::plugins::display {

struct feature {
  static constexpr std::string_view name = "display";
};

struct animations_page {
  friend bool operator==(animations_page, animations_page) = default;
};
struct rendering_page {
  config::renderer_t renderer;
  friend bool operator==(const rendering_page&, const rendering_page&) = default;
};
// Its own settings (contract::settings_kept), kept in the settings file
// under its name: how much the window moves (put in force by its part);
// what draws the window, from the next start; how frames
// are drawn -- only the damage repainted, and it outlined; in step with the
// display; the frames a second counted -- in effect at once.
struct kept {
  config::motion_t motion = config::motion::full{};
  config::renderer_t renderer = config::renderer::opengl{};
  bool partial_redraw = false;
  bool flash_redraws = false;
  bool vsync = true;
  bool show_fps = false;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
// The window made with the renderer chosen (main).
consteval bool provides(contract::start_renderer, type_tag<feature>) { return true; }
inline config::renderer_t renderer_at_start(contract::start_renderer, type_tag<feature>, const kept& mine) { return mine.renderer; }
struct open_animations {};
struct open_rendering {};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<animations_page, rendering_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::display
