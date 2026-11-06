// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.pins.changes -- What an account tells of pins: the messages
// pinned in a chat now -- put in the pins' part of the chat (applied_to).
// Light: under mux.core, on its ids alone.
export module mux.plugin.pins.changes;

import std;
import mux.core.ids;
import mux.plugin.pins.model;

export namespace mux::plugins::pins {

struct pinned_now {
  conversation_id in;
  std::vector<std::string> pinned;
};
inline void applied_to(const pinned_now& one, conversation& where) { std::get<chat_part<message>>(where.plugins).pinned = one.pinned; }
using changes = mux::change_list<pinned_now>;
inline std::string describe(const pinned_now&) { return std::string(); }

// The pinned of a chat: none where its protocol keeps no pins.
[[nodiscard]] inline const std::vector<std::string>& pinned_of(const conversation& chat) {
  return held_ids(std::get<chat_part<message>>(chat.plugins));
}

}  // namespace mux::plugins::pins
