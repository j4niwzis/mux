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

template <class>
struct state_list;
template <class... Tags>
struct state_list<protocol_list<Tags...>> {
  using type = spl::variant<state_of<Tags>...>;
};
using protocol_state_t = state_list<protocols>::type;

// A chat's protocol part: none, or one of a protocol's (room_parts(state)).
namespace room_part_defaults {
constexpr proto::room_part_list<> room_parts(const auto&) { return {}; }
}  // namespace room_part_defaults
template <class State>
constexpr auto room_parts_of(const State& state) {
  using room_part_defaults::room_parts;
  return room_parts(state);
}
template <class... Lists>
struct room_part_union;
template <class... Ps>
struct room_part_union<proto::room_part_list<Ps...>> {
  using type = spl::variant<proto::no_room_part, Ps...>;
};
template <class... As, class... Bs, class... Rest>
struct room_part_union<proto::room_part_list<As...>, proto::room_part_list<Bs...>, Rest...>
    : room_part_union<proto::room_part_list<As..., Bs...>, Rest...> {};
template <class>
struct room_part_list_of;
template <class... Tags>
struct room_part_list_of<protocol_list<Tags...>> {
  using type = typename room_part_union<proto::room_part_list<>, decltype(room_parts_of(state_of<Tags>{}))...>::type;
};
using room_part_t = typename room_part_list_of<protocols>::type;

// A message's protocol part: none, or one of a protocol's (message_parts).
namespace message_part_defaults {
constexpr proto::message_part_list<> message_parts(const auto&) { return {}; }
}  // namespace message_part_defaults
template <class State>
constexpr auto message_parts_of(const State& state) {
  using message_part_defaults::message_parts;
  return message_parts(state);
}
template <class... Lists>
struct message_part_union;
template <class... Ps>
struct message_part_union<proto::message_part_list<Ps...>> {
  using type = spl::variant<proto::no_message_part, Ps...>;
};
template <class... As, class... Bs, class... Rest>
struct message_part_union<proto::message_part_list<As...>, proto::message_part_list<Bs...>, Rest...>
    : message_part_union<proto::message_part_list<As..., Bs...>, Rest...> {};
template <class>
struct message_part_list_of;
template <class... Tags>
struct message_part_list_of<protocol_list<Tags...>> {
  using type = typename message_part_union<proto::message_part_list<>, decltype(message_parts_of(state_of<Tags>{}))...>::type;
};
// Compared as messages are: the same part, the same within it.
struct message_part {
  typename message_part_list_of<protocols>::type is;
  friend bool operator==(const message_part& a, const message_part& b) {
    return spl::visit(
        [&](const auto& mine) {
          return spl::visit(spl::overloaded{[&](const std::remove_cvref_t<decltype(mine)>& theirs) { return mine == theirs; },
                                                  [](const auto&) { return false; }},
                               b.is);
        },
        a.is);
  }
};

// A protocol's state before its account says anything: its type's default.
[[nodiscard]] inline protocol_state_t state_before(const protocol_t& speaks) {
  return spl::visit([](auto of) { return protocol_state_t{state_of<decltype(of)>{}}; }, speaks);
}

}  // namespace mux
