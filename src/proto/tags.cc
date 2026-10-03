// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.tags -- The protocols mux speaks, and the one list of them. A
// protocol is its state type, in a namespace of its own, mux::proto::<p> --
// where all it specifies is -- and known by id<state> (its "tag"). A protocol is added here, as its
// tag in the list, and by its overloads -- src/proto/<name>/, found by ADL
// through mux.protocols. Nothing else in mux names a protocol to decide
// what it does: it asks the protocol (mux.proto's extension points).
export module mux.proto.tags;

import std;
import splice;
export import mux.proto.identity;
export import mux.proto.xmpp.state;
export import mux.proto.matrix.state;


export namespace mux {

// The list: what protocol_t is one of, and what is asked in turn where a
// protocol has to be found -- an address's, a link's.
template <class... Tags>
struct protocol_list {};
using protocols = protocol_list<proto::xmpp::tag, proto::matrix::tag>;

template <class>
struct variant_of_list;
template <class... Tags>
struct variant_of_list<protocol_list<Tags...>> {
  using type = splice::variant<Tags...>;
};
using protocol_t = variant_of_list<protocols>::type;

// The names the code has known them by.
namespace protocol {
using xmpp = mux::proto::xmpp::tag;
using matrix = mux::proto::matrix::tag;
}  // namespace protocol

}  // namespace mux
