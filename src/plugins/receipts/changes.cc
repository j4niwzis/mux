// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.changes -- What an account tells of read receipts:
// who has read up to which message, and when -- put in the receipts' part of
// the chat (applied_to), each one's moved on only. Light: under mux.core, on
// its ids alone.
export module mux.plugin.receipts.changes;

import std;
import mux.core.ids;
import mux.plugin.receipts.model;

export namespace mux::plugins::receipts {

struct read {
  conversation_id in;
  std::map<std::string, std::string> read_by;  // user -> the message read up to
  // And when: the receipt's own time, for one read up to an event not among
  // the messages here -- a reaction, a state event, one not loaded.
  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>> read_at{};
};
inline void applied_to(const read& one, conversation& where) {
  auto& mine = std::get<chat_part<message>>(where.plugins);
  const auto place = [&](const std::string& id) { return std::ranges::find(where.timeline, id, &message::id); };
  for (const auto& [user, event] : one.read_by) {
    const auto previous = mine.read_by.find(user);
    bool accept = previous == mine.read_by.end() || previous->second == event;
    if (!accept) {
      const auto before = place(previous->second), after = place(event);
      if (before != where.timeline.end() && after != where.timeline.end())
        accept = after > before;
      else {
        const auto old_time = mine.times.find(user);
        const auto new_time = one.read_at.find(user);
        if (old_time != mine.times.end() && new_time != one.read_at.end())
          accept = new_time->second > old_time->second;
      }
      // Without either positions or timestamps, an unknown event cannot
      // prove that an existing receipt moved forward.
    }
    if (!accept)
      continue;
    mine.read_by.insert_or_assign(user, event);
    if (const auto time = one.read_at.find(user); time != one.read_at.end()) {
      const auto old = mine.times.find(user);
      if (old == mine.times.end() || time->second > old->second)
        mine.times.insert_or_assign(user, time->second);
    }
  }
}
using changes = mux::change_list<read>;
inline std::string describe(const read& one) { return std::format("{} read receipts", one.read_by.size()); }

}  // namespace mux::plugins::receipts
