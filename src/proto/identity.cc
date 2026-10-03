// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.identity -- What a protocol is known by: id<State>, made
// from its state type -- no type of its own to write. Compared and ordered
// (an account's id holds it), and found by ADL in the protocol's namespace
// through its template argument, so that an overload of mux::proto::<p> on
// it is found.
export module mux.proto.identity;

import std;

export namespace mux::proto {

// Its comparisons written out, not defaulted: clang 23 crashes generating a
// defaulted friend operator<=> of a module's type in a unit importing it
// ("Generating code for declaration ...::operator<=>"). One protocol is one
// value: every two of it are equal.
template <class State>
struct id {
  friend constexpr bool operator==(id, id) noexcept { return true; }
  friend constexpr std::strong_ordering operator<=>(id, id) noexcept { return std::strong_ordering::equal; }
};

}  // namespace mux::proto

export namespace mux {
// A type, carried as a value: what an extension point returns to name a
// type (its form, its page, what it keeps), and what is passed to name one.
// The program's own, not type_tag -- a transformation trait the
// standard never promises can be made.
template <class T>
struct type_tag {
  using type = T;
};
}  // namespace mux
