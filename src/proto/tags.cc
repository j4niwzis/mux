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
using protocols = protocol_list<proto::id<proto::xmpp::state>, proto::id<proto::matrix::state>>;

// A protocol's state type, from what it is known by.
template <class Tag>
struct state_of_t;
template <class State>
struct state_of_t<proto::id<State>> {
  using type = State;
};
template <class Tag>
using state_of = typename state_of_t<Tag>::type;

template <class>
struct variant_of_list;
template <class... Tags>
struct variant_of_list<protocol_list<Tags...>> {
  using type = spl::variant<Tags...>;
};
using protocol_t = variant_of_list<protocols>::type;

// The names the code has known them by.
namespace protocol {
using xmpp = mux::proto::id<mux::proto::xmpp::state>;
using matrix = mux::proto::id<mux::proto::matrix::state>;
}  // namespace protocol

}  // namespace mux
