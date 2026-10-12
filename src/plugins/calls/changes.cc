// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.calls.changes -- What a call says through its protocol and
// what its connection tells the program, the same with calls built in or
// not; and the calls plugin's own changes, what an account tells the
// program of a call -- joined into mux::change_t (mux.plugins.changes), and
// taken by the plugin's part. Light: under mux.core, on its ids alone.
export module mux.plugin.calls.changes;

import std;
import splice;
import mux.core.ids;

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

export namespace mux::plugins::calls {

// How a call ended, as the other side said it: Element's reasons, and any
// other as it came.
namespace call_end {
struct hung_up {};
struct busy {};
struct timed_out {};    // not answered in time
struct failed {};       // the connection, or the sound, could not be had
struct other {
  std::string said;
};
}  // namespace call_end
using call_end_t = spl::variant<call_end::hung_up, call_end::busy, call_end::timed_out, call_end::failed, call_end::other>;
// The version of the calls protocol a call is made in: Element's legacy
// calls (0), or version 1 -- what its invite said, and what is said back.
namespace call_version {
struct legacy {};
struct current {};
}  // namespace call_version
using call_version_t = spl::variant<call_version::legacy, call_version::current>;
namespace call_said {
struct invite {
  mux::calls::session_description offer;
  std::chrono::milliseconds lifetime{60000};
  call_version_t version = call_version::current{};
};
struct answer {
  mux::calls::session_description it;
};
struct candidates {
  std::vector<mux::calls::ice_candidate> them;
};
struct hangup {
  call_end_t why;
};
struct reject {};
// The caller's choice among those who answered: the one it talks to.
struct select_answer {
  std::string party;
};
}  // namespace call_said
using call_said_t =
    spl::variant<call_said::invite, call_said::answer, call_said::candidates, call_said::hangup, call_said::reject,
                 call_said::select_answer>;
// A call's signalling, as its protocol carried it: what was said in one
// call, by the other side or by the user's own other session. Protocol-
// neutral: each protocol's side of the plugin reads its events into it.
struct call_signalled {
  conversation_id in;
  std::string call;    // the call's id
  std::string party;   // the device that said it: one of several a person has
  std::string sender;
  bool mine = false;   // the user's own, from another session
  std::chrono::sys_time<std::chrono::milliseconds> at{};
  call_said_t said;
};
// The servers a call of an account goes through, as its server gave them
// (Matrix's /voip/turnServer): asked as a call starts.
struct call_servers {
  account_id account;
  std::vector<mux::calls::ice_server> servers;
  std::string party;
};
using changes = mux::change_list<call_signalled, call_servers>;

// Said on the command line (mux-cli), as every change is.
inline std::string describe(const call_signalled& one) {
  return std::format("call {} in {} from {}", one.call, one.in.id, one.sender);
}
inline std::string describe(const call_servers& one) { return std::format("{} call servers", one.servers.size()); }

}  // namespace mux::plugins::calls
