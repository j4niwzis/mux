// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.tags -- The protocols mux speaks: each a tag type in a namespace
// of its own, and the one list of them. A protocol is added here, as its
// tag in the list, and by its overloads -- src/proto/<name>.cc, found by ADL
// through mux.protocols. Nothing else in mux names a protocol to decide
// what it does: it asks the protocol (mux.proto's extension points).
export module mux.proto.tags;

import std;
import splice;

export namespace mux::xmpp {
struct tag {
  friend auto operator<=>(const tag&, const tag&) = default;
};
}  // namespace mux::xmpp

export namespace mux::matrix {
struct tag {
  friend auto operator<=>(const tag&, const tag&) = default;
};
}  // namespace mux::matrix

export namespace mux {

// The list: what protocol_t is one of, and what is asked in turn where a
// protocol has to be found -- an address's, a link's.
template <class... Tags>
struct protocol_list {};
using protocols = protocol_list<xmpp::tag, matrix::tag>;

template <class>
struct variant_of_list;
template <class... Tags>
struct variant_of_list<protocol_list<Tags...>> {
  using type = splice::variant<Tags...>;
};
using protocol_t = variant_of_list<protocols>::type;

// The names the code has known them by.
namespace protocol {
using xmpp = mux::xmpp::tag;
using matrix = mux::matrix::tag;
}  // namespace protocol

}  // namespace mux
