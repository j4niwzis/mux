// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.notifications -- Which messages are new to this run, across
// account reconnects, sync replays and changes of the visible timeline.
export module mux.logic.notifications;

import std;
import mux.core.ids;

export namespace mux::logic {

class notification_history {
 public:
  explicit notification_history(std::chrono::sys_time<std::chrono::milliseconds> started =
                                    std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()))
      : started_at_(started) {}

  // Called before checking focus or notification settings: a message read
  // in its chat, or received while muted, must not notify on a later replay.
  [[nodiscard]] bool first(const message& said) {
    if (said.at < started_at_ - std::chrono::minutes(1) || said.outgoing || said.service)
      return false;
    // Some protocols allow messages without IDs. They cannot be deduplicated
    // reliably; an empty ID must not suppress every later anonymous message.
    return said.id.empty() || seen_[said.in].insert(said.id).second;
  }

 private:
  std::chrono::sys_time<std::chrono::milliseconds> started_at_;
  // Kept for this run, independently of connections and the bounded message
  // cache: trimming a timeline must not make a replay new again.
  std::map<conversation_id, std::set<std::string>> seen_;
};

}  // namespace mux::logic
