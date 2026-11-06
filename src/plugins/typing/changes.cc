// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.changes -- What an account tells of typing: who is
// typing in a chat now -- put in the typing's part of the chat (applied_to);
// none, that no one is. Light: under mux.core, on its ids alone.
export module mux.plugin.typing.changes;

import std;
import mux.core.ids;
import mux.plugin.typing.model;

export namespace mux::plugins::typing {

struct typing_now {
  conversation_id in;
  std::vector<std::string> who;
};
inline void applied_to(const typing_now& one, conversation& where) { std::get<chat_part<message>>(where.plugins).who = one.who; }
using changes = mux::change_list<typing_now>;
inline std::string describe(const typing_now& one) {
  return one.who.empty() ? std::string() : std::format("{} typing in {}", one.who.size(), one.in.id);
}

}  // namespace mux::plugins::typing
