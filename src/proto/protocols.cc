// SPDX-License-Identifier: AGPL-3.0-only
// mux.protocols -- Every protocol's overloads, and the extension points that
// find them: what the rest of mux imports to ask a protocol anything. A
// protocol is added to the list in mux.proto.tags, and its module here.
export module mux.protocols;

import std;
import splice;
export import mux.proto.tags;
export import mux.proto.kept;
export import mux.proto;
export import mux.proto.xmpp;
export import mux.proto.matrix;

export namespace mux::proto {

// The protocol an address is of: the first in the list that owns it -- the
// list's first where none does.
template <class... Tags>
[[nodiscard]] protocol_t protocol_of(protocol_list<Tags...>, std::string_view address) {
  std::optional<protocol_t> found;
  (void)((owns_address(Tags{}, address) ? (found = protocol_t{Tags{}}, true) : false) || ...);
  return found.value_or(protocol_t{});
}
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) { return protocol_of(protocols{}, address); }

}  // namespace mux::proto
