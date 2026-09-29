// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.messages: what is done to a message from its menu, decided from
// the message alone -- a reaction put or taken back, a reply's line.
export module mux.logic.messages;

import std;
import mux.core;

export namespace mux::logic {

// A reaction pressed: put where the user's is not, taken back where it is.
[[nodiscard]] inline bool reaction_turns_on(const message& said, const std::string& key, const std::string& me) {
  const auto who = said.reactions.find(key);
  return who == said.reactions.end() || !who->second.contains(me);
}

// The line a reply shows of the message it answers, as tdesktop's: its
// text on one line, or what it carries where it has none.
[[nodiscard]] inline std::string reply_line(const message* said, std::string text) {
  if (text.empty() && said && said->attachment)
    text = is_picture(said->attachment->kind) ? std::string("Photo") : said->attachment->name;
  std::ranges::replace(text, '\n', ' ');
  return text;
}

}  // namespace mux::logic
