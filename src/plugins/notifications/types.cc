// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.notifications.types -- What the notifications plugin names,
// and what it says it provides: a part of the program. Light: mux.ui sees
// it.
export module mux.plugin.notifications.types;

import std;
import mux.core;
import mux.composition;
import mux.config;

export namespace mux::plugins::notifications {

struct feature {
  static constexpr std::string_view name = "notifications";
};

consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }
// Its page of Settings, and the request that opens it.
struct settings_page {
  config::notification_settings now;
  friend bool operator==(const settings_page&, const settings_page&) = default;
};
struct open_settings {};
consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<settings_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::notifications
