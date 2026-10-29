// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.devtools.changes -- What an account tells the program for the
// developer tools -- a message's source, a room's state and account data as
// synced, the answer to a write -- joined into mux::change_t
// (mux.plugins.changes), and taken by the plugin's part. Light: under
// mux.core, on its ids alone.
export module mux.plugin.devtools.changes;

import std;
import mux.core.ids;

export namespace mux::plugins::devtools {

// A state event of a room, as listed: its type and key, and as JSON to
// read -- the whole event, and its content (what an edit starts from).
struct state_entry {
  std::string type, key, json, content;
  friend bool operator==(const state_entry&, const state_entry&) = default;
};
// A room's account data, one's own: its type and content, as JSON.
struct data_entry {
  std::string type, content;
  friend bool operator==(const data_entry&, const data_entry&) = default;
};

// Some JSON, or what a write came to, to read: under its title, for the
// room it was asked in.
struct told {
  conversation_id in;
  std::string title, text;
};
// A room's state, as the account has it synced.
struct state_listed {
  conversation_id in;
  std::vector<state_entry> entries;
};
// A room's account data, as the account has it synced.
struct data_listed {
  conversation_id in;
  std::vector<data_entry> entries;
};
using changes = mux::change_list<told, state_listed, data_listed>;

inline std::string describe(const told& one) { return one.title; }
inline std::string describe(const state_listed& one) { return std::format("{} state events", one.entries.size()); }
inline std::string describe(const data_listed& one) { return std::format("{} account data", one.entries.size()); }

}  // namespace mux::plugins::devtools
