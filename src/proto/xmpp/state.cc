// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.xmpp.state -- What an XMPP account is now, as XMPP's extension
// points decide by it: the stream, and what the server said it has (its
// disco#info, XEP-0030) -- read once, where it comes in, into what it means.
export module mux.proto.xmpp.state;

import std;
import mux.proto.identity;

export namespace mux::proto::xmpp {

struct state {
  bool online = false;  // the stream is up
  // The server's message archive (XEP-0313, urn:xmpp:mam:2); unset where it
  // has not been asked yet.
  std::optional<bool> archive;
  friend bool operator==(const state&, const state&) = default;
};
// What the protocol is known by: its state type, as an identity.
using tag = id<state>;

}  // namespace mux::proto::xmpp
