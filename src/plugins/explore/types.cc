// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.explore.types -- What the explore plugin names, and what it
// says it provides: its button in the side's head, the dialog the
// directory is explored in, and a part of the program. Light: mux.ui sees
// it.
export module mux.plugin.explore.types;

import std;
import mux.core;
import mux.composition;

export namespace mux::plugins::explore {

struct feature {
  static constexpr std::string_view name = "explore";
};

// What a directory or a space listed: its rooms, of which server or space,
// for what query, and where the next page starts.
struct listing {
  std::vector<directory_room> rooms;
  std::string server;
  std::optional<std::string> space;
  std::string query;
  std::optional<std::string> next;
  friend bool operator==(const listing&, const listing&) = default;
};
// The space explored, by its room and name.
struct space_shown {
  std::string room, name;
  friend bool operator==(const space_shown&, const space_shown&) = default;
};
// Explore open: the account's own server, what is listed, whether more is
// being asked, the space explored, by which account, and the search out --
// which answers belong to it.
struct shown {
  std::string own_server;
  std::optional<listing> listed;
  bool loading = false;
  std::optional<space_shown> space;
  std::optional<account_id> by;
  std::string server, query;
  friend bool operator==(const shown&, const shown&) = default;
};
// Whether an answer is for the search open: the same account, space, server
// and query.
inline bool accepts_listing(const shown& now, const change::directory_listed& one) {
  return now.by == std::optional(one.by) && (now.space ? std::optional(now.space->room) : std::nullopt) == one.space &&
         (one.space || (now.server == one.server && now.query == one.query));
}

// Asked of the program: Explore opened (on the listed account's server); a
// room of what is listed joined, through the server it was listed by.
struct open {};
struct join {
  std::string room, server;
  std::optional<account_id> by;
};

consteval bool provides(contract::side_header, type_tag<feature>) { return true; }
consteval bool provides(contract::window_dialogs, type_tag<feature>) { return true; }
constexpr type_tag<shown> facts_type(contract::window_dialogs, type_tag<feature>) { return {}; }
consteval bool provides(contract::program_parts, type_tag<feature>) { return true; }

}  // namespace mux::plugins::explore
