// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.pins.model -- What pins keep in the model, beside the client's
// own (mux.plugins.model): of a chat, the messages pinned in it, by their
// ids, oldest first -- none where its protocol keeps no pins; nothing of a
// message. And the messages it holds on to (held_ids): the pinned, kept
// among the quoted and fetched where they are not loaded, for the bar. On
// std alone.
export module mux.plugin.pins.model;

import std;

export namespace mux::plugins::pins {

struct message_part {
  friend bool operator==(const message_part&, const message_part&) = default;
};
template <class Message>
struct chat_part {
  // The pinned, as its protocol last said; none where it keeps no pins (an
  // XMPP chat): nothing there may be pinned.
  std::optional<std::vector<std::string>> pinned;
  friend bool operator==(const chat_part&, const chat_part&) = default;
};

template <class Message>
[[nodiscard]] const std::vector<std::string>& held_ids(const chat_part<Message>& mine) {
  static const std::vector<std::string> none;
  return mine.pinned ? *mine.pinned : none;
}

}  // namespace mux::plugins::pins
