// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.changes -- What an account tells of threads: a room's,
// as its server lists them -- their roots, newest first (each root's own
// message comes aside, with its summary) -- put in the threads' part of the
// chat (applied_to). Light: under mux.core, on its ids alone.
export module mux.plugin.threads.changes;

import std;
import mux.core.ids;

export namespace mux::plugins::threads {

struct listed {
  conversation_id in;
  std::vector<std::string> roots;
};
inline void applied_to(const listed& one, conversation& where) {
  std::get<chat_part<message>>(where.plugins).roots = one.roots;
}
using changes = mux::change_list<listed>;
inline std::string describe(const listed& one) { return std::format("{} threads", one.roots.size()); }

}  // namespace mux::plugins::threads
