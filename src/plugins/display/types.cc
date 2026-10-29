// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.display.types -- What the display plugin names, and what it
// says it provides: Settings' Animations and Rendering pages, their links,
// and a part of the program that opens them. Light: mux.ui sees it.
export module mux.plugin.display.types;

import std;
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
struct open_animations {};
struct open_rendering {};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<animations_page, rendering_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::display
