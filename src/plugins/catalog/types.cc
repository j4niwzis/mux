// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.catalog.types -- What the catalog plugin names, and what it
// says it provides: Settings' link to the Plugins page, its pages (the
// list, and a plugin's own), and a part of the program that opens them.
// Light: mux.ui sees it.
export module mux.plugin.catalog.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::catalog {

struct feature {
  static constexpr std::string_view name = "catalog";
};

// Its pages of Settings: the plugins listed; one plugin's, by its place
// among them (mux::plugin_infos).
struct list_page {
  friend bool operator==(list_page, list_page) = default;
};
struct plugin_page {
  std::size_t index = 0;
  friend bool operator==(const plugin_page&, const plugin_page&) = default;
};
// Asked of the program: the list opened; a plugin's page.
struct open_list {};
struct open_plugin {
  std::size_t index = 0;
};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<list_page, plugin_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::catalog
