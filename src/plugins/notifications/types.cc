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
// An invite already told of: whose account, to which room -- kept, so that
// one is told once, not again on every start (a tester's).
struct told_invite {
  std::string account;
  std::string room;
  friend bool operator==(const told_invite&, const told_invite&) = default;
  friend consteval auto json_schema(knot::type<told_invite>) { return knot::schema<told_invite>(); }
};
struct kept {
  std::optional<std::string> sound_file;
  std::optional<bool> unified_push;
  std::optional<std::string> push_token;
  std::optional<std::string> push_endpoint;
  // The distributor chosen, by its bus name (org.unifiedpush.Distributor.*);
  // none: the first there is.
  std::optional<std::string> push_distributor;
  std::vector<told_invite> invites_told;
  friend bool operator==(const kept&, const kept&) = default;
  friend consteval auto json_schema(knot::type<kept>) { return knot::schema<kept>(); }
};
// What the window shows of UnifiedPush (contract::window_state): the
// distributors found on the bus, the one registered with, and how it went.
struct push_shown {
  std::vector<std::string> distributors;
  std::string chosen;
  std::string status;  // registered, refused, none found...
  std::optional<std::string> endpoint;
  friend bool operator==(const push_shown&, const push_shown&) = default;
};
consteval bool provides(contract::window_state, type_tag<feature>) { return true; }
constexpr type_tag<push_shown> state_type(contract::window_state, type_tag<feature>) { return {}; }
// Asked of the program: a distributor chosen (its place in the list); the
// registration made again.
struct choose_distributor {
  std::size_t index = 0;
};
struct register_again {};
consteval bool provides(contract::settings_kept, type_tag<feature>) { return true; }
constexpr type_tag<kept> settings_type(contract::settings_kept, type_tag<feature>) { return {}; }
consteval bool provides(contract::settings_links, type_tag<feature>) { return true; }
consteval bool provides(contract::settings_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<settings_page> settings_pages(contract::settings_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::notifications
