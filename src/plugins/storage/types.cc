// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.storage.types -- What the storage plugin names, and what it
// says it provides: Settings' Storage and Files pages, their links, and a
// part of the program that opens them. Light: mux.ui sees it.
export module mux.plugin.storage.types;

import std;
import mux.core;
import mux.config;
import mux.composition;

export namespace mux::plugins::storage {

struct feature {
  static constexpr std::string_view name = "storage";
};

struct storage_page {
  config::cache_limits limits;
  config::history_settings history;
  bool sealed = false;
  friend bool operator==(const storage_page&, const storage_page&) = default;
};
struct files_page {
  config::sending_settings now;
  friend bool operator==(const files_page&, const files_page&) = default;
};
struct open_storage {};
struct open_files {};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<storage_page, files_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::storage
