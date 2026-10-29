// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.profiles.types -- What the profiles plugin names, and what it
// says it provides: an account's Profile page. Light: mux.ui sees it.
export module mux.plugin.profiles.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::profiles {

struct feature {
  static constexpr std::string_view name = "profiles";
};

struct profile_page {
  friend bool operator==(profile_page, profile_page) = default;
};

consteval bool provides(contract::account_pages, type_tag<feature>) { return true; }
constexpr composition::page_list<profile_page> account_pages(contract::account_pages, type_tag<feature>) { return {}; }

}  // namespace mux::plugins::profiles
