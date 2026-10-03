// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.state -- Each protocol's account state (mux::proto::<protocol>::
// state, what the protocol is known by too), and protocol_state_t, one of them,
// made from the list: what a protocol's extension points are asked with --
// the state every overload of the protocol shares, its server's facts. A
// protocol is added here beside its tag.
export module mux.proto.state;

import std;
import splice;
export import mux.proto.tags;

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
struct state_list;
template <class... Tags>
struct state_list<protocol_list<Tags...>> {
  using type = splice::variant<state_of<Tags>...>;
};
using protocol_state_t = state_list<protocols>::type;

// A protocol's state before its account says anything: its type's default.
[[nodiscard]] inline protocol_state_t state_before(const protocol_t& speaks) {
  return splice::visit([](auto tag) { return protocol_state_t{state_of<decltype(tag)>{}}; }, speaks);
}

}  // namespace mux
