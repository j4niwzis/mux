// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.commands.changes -- What an account tells the program for the
// commands -- the event of a room at a time, as its server found it --
// joined into mux::change_t (mux.plugins.changes), and taken by the
// plugin's part. Light: under mux.core, on its ids alone.
export module mux.plugin.commands.changes;

import std;
import mux.core.ids;

export namespace mux::plugins::commands {

// The room's first event from a time on, as its server said (Matrix's
// timestamp_to_event): none where it found none.
struct event_at {
  conversation_id in;
  std::optional<std::string> event;
};
using changes = mux::change_list<event_at>;

inline std::string describe(const event_at& one) { return one.event ? std::format("event at a time: {}", *one.event) : "no event at that time"; }

}  // namespace mux::plugins::commands
