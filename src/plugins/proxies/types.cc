// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.proxies.types -- What the proxies plugin names, and what it
// says it provides: its link and its two pages in Settings, an account's
// Proxy page, what it keeps among what the window shows, and a part of the
// program. Light: mux.ui sees it.
export module mux.plugin.proxies.types;

import std;
import mux.config;
import mux.composition;

export namespace mux::plugins::proxies {

struct feature {
  static constexpr std::string_view name = "proxies";
};

// Its pages in Settings: the profiles, with or without the arrow back to
// Settings' home (opened from an account: without); and one profile edited,
// or a new one -- by its place, -1 new.
struct list_page {
  std::vector<config::proxy_settings> all;
  bool with_back = true;
};
struct editor_page {
  std::optional<config::proxy_settings> from;
  int index = -1;
};
// An account's Proxy page.
struct account_page {};
// What it keeps among what the window shows: the profiles' names, which each
// account goes through, and what went wrong saving one.
struct shown {
  std::vector<std::string> names;
  std::vector<std::string> labels;  // each, as Home (SOCKS5 localhost:1080)

  std::map<std::string, std::string> through;  // an account's address: its profile's name
  std::string error;
  friend bool operator==(const shown&, const shown&) = default;
};

// Asked of the program: the profiles, from Settings' link or an editor's
// arrow; from an account's page or as one is added (Settings opened on
// them); a profile added, edited, saved -- the profile the editor's draft
// makes, or why it makes none -- or deleted; and the account's chosen, by
// its place (-1: none).
struct open_list {};
struct manage {};
struct add {};
struct edit {
  int index = -1;
};
struct save {
  std::expected<config::proxy_settings, std::string> profile;
  int index = -1;
};
struct remove {
  int index = -1;
};
struct choose {
  std::string account;
  int index = -1;
};

consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<list_page, editor_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::account_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<account_page> account_pages(contract::account_pages, type_tag<feature>) { return {}; }
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::proxies
