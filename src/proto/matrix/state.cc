// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.state -- What a Matrix account is now, as Matrix's
// extension points decide by it.
export module mux.proto.matrix.state;

import std;
import mux.proto.tags;

export namespace mux::proto::matrix {

struct state {
  bool online = false;  // syncing
  friend bool operator==(const state&, const state&) = default;
};
constexpr std::type_identity<state> state_type(tag) { return {}; }

}  // namespace mux::proto::matrix
