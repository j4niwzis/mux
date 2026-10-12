// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.tags -- The protocols mux speaks, and what is found from their
// list. A protocol is its state type, in a namespace of its own,
// mux::proto::<p> -- where all it specifies is -- and known by id<state> (its
// "tag"). A protocol is its folder, src/proto/<name>/, with its own
// CMakeLists.txt, and its option, MUX_PROTOCOL_<NAME>: the list of those
// turned on -- mux::protocols -- and each one's tag by its name are written by
// CMake (mux.proto.enabled.list, cmake/protocols.cmake). Its overloads are
// found by ADL through mux.protocols. Nothing else in mux names a protocol to
// decide what it does: it asks the protocol (mux.proto's extension points).
export module mux.proto.tags;

import std;
import splice;
export import mux.proto.identity;
export import mux.proto.enabled.list;


export namespace mux {

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

}  // namespace mux
