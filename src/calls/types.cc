// SPDX-License-Identifier: AGPL-3.0-only
// mux.calls.types -- What a call says through its protocol and what its
// connection tells the program: the same with calls built in or not.
export module mux.calls.types;

import std;
import splice;

export namespace mux::calls {

// A server a call's connection goes through: STUN to learn its address,
// TURN to be relayed where nothing else gets through -- over UDP, TCP or
// TLS. As the protocol gave it, read once into this.
namespace relay {
struct stun {};
struct turn_udp {};
struct turn_tcp {};
struct turn_tls {};
}  // namespace relay
using relay_t = spl::variant<relay::stun, relay::turn_udp, relay::turn_tcp, relay::turn_tls>;
struct ice_server {
  relay_t kind;
  std::string host;
  std::uint16_t port = 3478;
  std::string username;
  std::string password;
};

// A session description: an offer or an answer, and its SDP -- what the
// protocol carries, never looked inside but by the connection.
namespace sdp_kind {
struct offer {};
struct answer {};
}  // namespace sdp_kind
using sdp_kind_t = spl::variant<sdp_kind::offer, sdp_kind::answer>;
struct session_description {
  sdp_kind_t kind;
  std::string sdp;
};
// A candidate address, as ICE gives one: its line, and the media it is for.
struct ice_candidate {
  std::string line;
  std::string mid;
};

// What a call's connection told since the program last asked.
namespace said {
struct description {
  session_description it;
};
struct candidate {
  ice_candidate it;
};
struct connected {};
struct failed {};
struct ended {};
}  // namespace said
using said_t = spl::variant<said::description, said::candidate, said::connected, said::failed, said::ended>;

}  // namespace mux::calls
