// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.notifications.types -- What the notifications plugin names,
// and what it says it provides: a part of the program. Light: mux.ui sees
// it.
export module mux.plugin.notifications.types;

import std;
import mux.core;
import mux.composition;
import knot;
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
// Its own settings (contract::settings_kept), kept under its name: a sound of
// one's own; and UnifiedPush -- off unless chosen: only then is the bus
// asked, and the servers given a pusher -- with the connection's token, kept
// across runs, and the endpoint the distributor gave for it.
struct kept {
  std::optional<std::string> sound_file;
  std::optional<bool> unified_push;
  std::optional<std::string> push_token;
  std::optional<std::string> push_endpoint;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<settings_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::notifications
