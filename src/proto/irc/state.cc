// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.irc.state -- What an IRC account is now, as IRC's extension points
// decide by it: the connection, and what the server said it has -- its
// capabilities and its ISUPPORT -- read once, where they come in, into what
// they mean.
export module mux.proto.irc.state;

import std;
import mux.proto.identity;

export namespace mux::proto::irc {

// What a channel keeps beyond what every chat has: whether one is in it
// now (a channel is kept in the list after a disconnection, to be joined
// again), and who set its topic.
struct channel_part {
  bool joined = false;
  std::string topic_by;
  friend bool operator==(const channel_part&, const channel_part&) = default;
};

struct state {
  bool online = false;  // registered: the server said 001
  // IRCv3 chathistory: older lines asked of the server.
  bool history = false;
  // draft/read-marker: where one has read up to, kept on the server.
  bool read_marker = false;
  // The network's name, as ISUPPORT's NETWORK says; and what begins a
  // channel's name (CHANTYPES).
  std::string network;
  std::string chantypes = "#&";
  friend bool operator==(const state&, const state&) = default;
};
constexpr room_part_list<channel_part> room_parts(const state&) { return {}; }

// The protocol is known by its state type itself (id<state>, in protocol_t);
// every overload of it takes its state.

}  // namespace mux::proto::irc
