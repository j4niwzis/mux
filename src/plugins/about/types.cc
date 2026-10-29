// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.about.types -- What the About plugin names, and what it says it
// provides: Settings' link to About, its pages (About, and a library's),
// and a part of the program that opens them. Light: mux.ui sees it.
export module mux.plugin.about.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::about {

struct feature {
  static constexpr std::string_view name = "about";
};

// Its pages of Settings: About, and one library's.
struct about_page {
  friend bool operator==(about_page, about_page) = default;
};
struct library_page {
  std::string name;
  friend bool operator==(const library_page&, const library_page&) = default;
};
// Asked of the program: About opened; a library's page.
struct open_about {};
struct open_library {
  std::string name;
};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<about_page, library_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::about
