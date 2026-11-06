// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.receipts.model -- What receipts keep in the model, beside the
// client's own (mux.plugins.model): of a chat, who has read up to which of
// its messages, as their receipts say, and when; nothing of a message. On
// std alone.
export module mux.plugin.receipts.model;

import std;

export namespace mux::plugins::receipts {

struct message_part {
  friend bool operator==(const message_part&, const message_part&) = default;
};
template <class Message>
struct chat_part {
  // Each other person's last message read (user -> its id); and when, where
  // their receipt says -- for one read up to what is not here: a reaction,
  // a state event, one not loaded.
  std::map<std::string, std::string> read_by;
  std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>> times;
  friend bool operator==(const chat_part&, const chat_part&) = default;
};

}  // namespace mux::plugins::receipts
