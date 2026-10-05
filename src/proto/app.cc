// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.proto -- Every protocol's overloads for the program itself: what it
// does with a protocol's own changes, besides the model. Each a template on
// the program (mux.app.program's app), found by ADL on the change where the
// program takes changes. A protocol with none is not added here: its changes
// come to the default, nothing done.
export module mux.app.proto;

export import mux.app.proto.matrix;
export import mux.app.proto.xmpp;

export namespace mux::app::defaults {
// A change the program does nothing with of its own: the model's alone.
inline void program_told(auto&, const auto&) {}
}  // namespace mux::app::defaults
