// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.files.types -- What the files plugin names, and what it says it
// provides: Settings' Files page and its link, its own settings -- what is
// done to a picture dropped before it is sent -- and a part of the program
// that opens the page and puts them in force. Light: mux.ui sees it.
export module mux.plugin.files.types;

import std;
import mux.core;
import mux.config;
import mux.composition;

export namespace mux::plugins::files {

struct feature {
  static constexpr std::string_view name = "files";
};

struct files_page {
  config::sending_settings now;
  friend bool operator==(const files_page&, const files_page&) = default;
};
struct open_files {};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<files_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// Its own settings (contract::settings_kept), kept in the settings file
// under its name; bound by its page, put in force by its part.
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<config::sending_settings> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::files
