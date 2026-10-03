// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.kept -- What every protocol keeps of its own in the accounts
// file: the modules mux.config reads them through. A protocol is added here
// beside its tag (mux.proto.tags).
export module mux.proto.kept;

export import mux.proto.tags;
export import mux.proto.xmpp.kept;
export import mux.proto.matrix.kept;

// What an account's own keeps through an edit, and of a session its server
// gave: nothing, unless its protocol says (carry_over, take_session, by ADL
// on what it keeps).
export namespace mux::proto::kept_defaults {
inline void carry_over(auto&, const auto&) {}
inline void take_session(auto&, const auto&) {}
}  // namespace mux::proto::kept_defaults
