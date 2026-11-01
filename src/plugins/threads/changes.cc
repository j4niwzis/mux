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
// A page of a thread's answers come (each came aside, a message in it):
// where the next older page starts, none once all are; or none come, the
// way back as it was.
struct paged {
  conversation_id in;
  std::string root;
  bool came = false;
  std::optional<std::string> next;
};
inline void applied_to(const paged& one, conversation& where) {
  if (one.came)
    std::get<chat_part<message>>(where.plugins).pages.insert_or_assign(one.root, one.next);
}
using changes = mux::change_list<listed, paged>;
inline std::string describe(const listed& one) { return std::format("{} threads", one.roots.size()); }
inline std::string describe(const paged& one) {
  return std::format("thread {}: {}", one.root, one.came ? (one.next ? "a page" : "the last page") : "no page");
}

}  // namespace mux::plugins::threads
