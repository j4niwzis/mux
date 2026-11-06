// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.typing.model -- What typing keeps in the model, beside the
// client's own (mux.plugins.model): of a chat, who is typing in it now, as
// its protocol last said; nothing of a message. And what it makes of what
// the model hears of a chat (mux.core's message_came, members_now): a
// message come live from someone ends their typing, and one who left the
// chat types no more. On std alone.
export module mux.plugin.typing.model;

import std;

export namespace mux::plugins::typing {

struct message_part {
  friend bool operator==(const message_part&, const message_part&) = default;
};
template <class Message>
struct chat_part {
  // Who is typing, by their ids in the chat.
  std::vector<std::string> who;
  friend bool operator==(const chat_part&, const chat_part&) = default;
};

// A message come live, at the chat's end: its sender has stopped typing.
template <class Message>
void message_came(chat_part<Message>& mine, const Message& one) {
  std::erase(mine.who, one.sender);
}
// Who was in the chat and is no longer -- left, kicked, banned -- types no
// more: their typing, said before they went, stayed under the name.
template <class Message, class Members>
void members_now(chat_part<Message>& mine, const Members& before, const Members& after) {
  const auto among = [](const Members& members, const std::string& who) {
    return std::ranges::any_of(members, [&](const auto& one) { return one.id == who; });
  };
  std::erase_if(mine.who, [&](const std::string& who) { return among(before, who) && !among(after, who); });
}

}  // namespace mux::plugins::typing
